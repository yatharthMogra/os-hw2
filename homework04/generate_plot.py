#!/usr/bin/env python3
"""
Generate performance comparison plot for mutex implementation
"""

import subprocess
import re
import matplotlib.pyplot as plt
import numpy as np

def run_benchmark(program, threads):
    """Run a benchmark and extract timing information"""
    result = subprocess.run([program, str(threads)], 
                          capture_output=True, text=True)
    output = result.stdout
    
    # Extract insert time
    insert_match = re.search(r'Inserted \d+ keys in ([\d.]+) seconds', output)
    insert_time = float(insert_match.group(1)) if insert_match else 0.0
    
    # Extract retrieve time
    retrieve_match = re.search(r'Retrieved \d+/\d+ keys in ([\d.]+) seconds', output)
    retrieve_time = float(retrieve_match.group(1)) if retrieve_match else 0.0
    
    return insert_time, retrieve_time

def main():
    # Compile programs
    print("Compiling programs...")
    subprocess.run(['gcc', '-pthread', 'parallel_hashtable.c', '-o', 'parallel_hashtable'], 
                  check=True)
    subprocess.run(['gcc', '-pthread', 'parallel_mutex.c', '-o', 'parallel_mutex'], 
                  check=True)
    
    # Compile spinlock version (uses atomic operations, works on both macOS and Linux)
    spinlock_available = True
    try:
        result = subprocess.run(['gcc', '-pthread', 'parallel_spin.c', '-o', 'parallel_spin'], 
                              capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError:
        print("Warning: Failed to compile parallel_spin.c")
        print("Skipping spinlock benchmarks.")
        spinlock_available = False
    
    # Test with different thread counts (multiples of 2 until 256, as per requirements)
    thread_counts = [1, 2, 4, 8, 16, 32, 64, 128, 256]
    original_times = []
    mutex_times = []
    spinlock_times = []
    
    print("Running benchmarks...")
    for threads in thread_counts:
        print(f"  Testing with {threads} threads...")
        
        # Run original (incorrect) version
        orig_insert, orig_retrieve = run_benchmark('./parallel_hashtable', threads)
        original_times.append(orig_insert + orig_retrieve)
        
        # Run mutex version
        mutex_insert, mutex_retrieve = run_benchmark('./parallel_mutex', threads)
        mutex_times.append(mutex_insert + mutex_retrieve)
        
        # Run spinlock version if available
        if spinlock_available:
            try:
                spin_insert, spin_retrieve = run_benchmark('./parallel_spin', threads)
                spinlock_times.append(spin_insert + spin_retrieve)
            except:
                spinlock_available = False
                spinlock_times.append(None)
        else:
            spinlock_times.append(None)
    
    # Create Plot 1: Mutex Comparison (Original vs Mutex)
    plt.figure(figsize=(12, 7))
    plt.plot(thread_counts, original_times, 'o-', label='Original (incorrect)', linewidth=2, markersize=6, color='red')
    plt.plot(thread_counts, mutex_times, 's-', label='Mutex (correct)', linewidth=2, markersize=6, color='blue')
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Total Time (insert + retrieve, seconds)', fontsize=12)
    plt.title('Performance Comparison: Original vs Mutex Implementation', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.xscale('log', base=2)
    plt.xticks(thread_counts, thread_counts)
    plt.tight_layout()
    plt.savefig('images/mutex_performance_comparison.png', dpi=300, bbox_inches='tight')
    print("\nPlot 1 saved as 'images/mutex_performance_comparison.png'")
    plt.close()
    
    # Create Plot 2: Spinlock Comparison (Original vs Mutex vs Spinlock)
    plt.figure(figsize=(12, 7))
    plt.plot(thread_counts, original_times, 'o-', label='Original (incorrect)', linewidth=2, markersize=6, color='red')
    plt.plot(thread_counts, mutex_times, 's-', label='Mutex (correct)', linewidth=2, markersize=6, color='blue')
    
    if spinlock_available and all(t is not None for t in spinlock_times):
        plt.plot(thread_counts, spinlock_times, '^-', label='Spinlock (correct)', linewidth=2, markersize=6, color='green')
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Total Time (insert + retrieve, seconds)', fontsize=12)
    plt.title('Performance Comparison: Original vs Mutex vs Spinlock', fontsize=14, fontweight='bold')
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.xscale('log', base=2)
    plt.xticks(thread_counts, thread_counts)
    plt.tight_layout()
    plt.savefig('images/spinlock_performance_comparison.png', dpi=300, bbox_inches='tight')
    print("Plot 2 saved as 'images/spinlock_performance_comparison.png'")
    plt.close()
    
    # Print summary statistics
    print("\nPerformance Summary:")
    if spinlock_available and all(t is not None for t in spinlock_times):
        print(f"{'Threads':<10} {'Original':<15} {'Mutex':<15} {'Spinlock':<15} {'Mutex Overhead':<15} {'Spin Overhead':<15}")
        print("-" * 85)
        for i, threads in enumerate(thread_counts):
            mutex_overhead = ((mutex_times[i] - original_times[i]) / original_times[i]) * 100
            spin_overhead = ((spinlock_times[i] - original_times[i]) / original_times[i]) * 100
            print(f"{threads:<10} {original_times[i]:<15.6f} {mutex_times[i]:<15.6f} {spinlock_times[i]:<15.6f} {mutex_overhead:<15.2f}% {spin_overhead:<15.2f}%")
        
        # Calculate average overhead
        avg_mutex_overhead = np.mean([(mutex_times[i] - original_times[i]) / original_times[i] * 100 
                                     for i in range(len(thread_counts))])
        avg_spin_overhead = np.mean([(spinlock_times[i] - original_times[i]) / original_times[i] * 100 
                                    for i in range(len(thread_counts))])
        print(f"\nAverage mutex overhead: {avg_mutex_overhead:.2f}%")
        print(f"Average spinlock overhead: {avg_spin_overhead:.2f}%")
    else:
        print(f"{'Threads':<10} {'Original':<15} {'Mutex':<15} {'Overhead':<15}")
        print("-" * 55)
        for i, threads in enumerate(thread_counts):
            overhead = ((mutex_times[i] - original_times[i]) / original_times[i]) * 100
            print(f"{threads:<10} {original_times[i]:<15.6f} {mutex_times[i]:<15.6f} {overhead:<15.2f}%")
        
        # Calculate average overhead
        avg_overhead = np.mean([(mutex_times[i] - original_times[i]) / original_times[i] * 100 
                                for i in range(len(thread_counts))])
        print(f"\nAverage mutex overhead: {avg_overhead:.2f}%")

if __name__ == '__main__':
    main()
