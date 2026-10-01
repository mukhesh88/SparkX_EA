"""
SparkX Terminal - High-Frequency SMC Trading Engine
Main entry point for running the Antigravity trading decision agent and latency benchmarking.
"""

import sys
import os
import asyncio
import time
import argparse

# Add current workspace root to sys.path
sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

from config.trading_config import SystemConfig
from src.antigravity_agent import SparkXTradingAgent
from src.compressor import MarketStateCompressor
from src.prompt_templates import LayaPromptTemplates

def print_banner():
    banner = r"""
================================================================================
   ____                  __   _  __  ______                 _             __
  / __/__  ___ ________ / / _| |/ / /_  __/__ ______ _  __ (_)__  ___ _  / /
 _\ \/ _ \/ _ `/ __/ // / /_/>   <   / / / -_) __/  ' \/ // / _ \/ _ `/ / / 
/___/ .__/\_,_/_/  \_,_/_/  /_/|_|  /_/  \__/_/ /_/_/_/_//_/_//_/\_,_/ /_/  
   /_/         Ultra-Low-Latency SMC Decision Agent (Antigravity + Laya)
================================================================================
    """
    print(banner)


async def run_latency_benchmark(agent: SparkXTradingAgent, iterations: int = 100):
    """
    Performs rigorous quantitative latency profiling over multiple iterations.
    Validates the sub-40ms end-to-end latency constraint and sub-400 token compression limit.
    """
    print(f"\n[*] Commencing Quantitative Latency Profiling ({iterations} iterations)...")
    symbols = ["XAUUSD"]
    
    total_latencies = []
    ingest_latencies = []
    smc_latencies = []
    compress_latencies = []
    inference_latencies = []
    execution_latencies = []
    token_counts = []

    # Warm-up pass
    for sym in symbols:
        await agent.process_symbol_event(sym)

    for i in range(iterations):
        for sym in symbols:
            t0 = time.perf_counter()

            # 1. Ingestion
            t1 = time.perf_counter()
            bars_m5 = agent.ingestion.fetch_bars(sym, "M5", count=60)
            bars_m15 = agent.ingestion.fetch_bars(sym, "M15", count=40)
            bars_h1 = agent.ingestion.fetch_bars(sym, "H1", count=40)
            bid, ask, spread = agent.ingestion.fetch_live_tick(sym)
            t_ingest = (time.perf_counter() - t1) * 1000.0

            # 2. SMC Extraction
            t2 = time.perf_counter()
            features = agent.smc_engines[sym].extract_features(bars_m5, bars_m15, bars_h1, spread)
            t_smc = (time.perf_counter() - t2) * 1000.0

            # 3. Compression
            t3 = time.perf_counter()
            compressed = MarketStateCompressor.compress(features)
            tokens = MarketStateCompressor.estimate_tokens(compressed)
            t_compress = (time.perf_counter() - t3) * 1000.0

            # 4. Laya Inference
            t4 = time.perf_counter()
            decision = await agent.laya.infer_confluent_decision(compressed)
            t_infer = (time.perf_counter() - t4) * 1000.0

            # 5. Gateway Execution
            t5 = time.perf_counter()
            receipt = agent.gateway.route_execution(decision, features)
            t_exec = (time.perf_counter() - t5) * 1000.0

            t_total = (time.perf_counter() - t0) * 1000.0

            ingest_latencies.append(t_ingest)
            smc_latencies.append(t_smc)
            compress_latencies.append(t_compress)
            inference_latencies.append(t_infer)
            execution_latencies.append(t_exec)
            total_latencies.append(t_total)
            token_counts.append(tokens)

    # Calculate statistics
    avg_total = sum(total_latencies) / len(total_latencies)
    p95_total = sorted(total_latencies)[int(len(total_latencies) * 0.95)]
    p99_total = sorted(total_latencies)[int(len(total_latencies) * 0.99)]
    min_total = min(total_latencies)
    max_total = max(total_latencies)

    avg_ingest = sum(ingest_latencies) / len(ingest_latencies)
    avg_smc = sum(smc_latencies) / len(smc_latencies)
    avg_comp = sum(compress_latencies) / len(compress_latencies)
    avg_infer = sum(inference_latencies) / len(inference_latencies)
    avg_exec = sum(execution_latencies) / len(execution_latencies)
    avg_tokens = sum(token_counts) / len(token_counts)

    print("\n" + "=" * 70)
    print("                LATENCY TELEMETRY BENCHMARK REPORT")
    print("=" * 70)
    print(f"Sample Size               : {len(total_latencies)} pipeline cycles")
    print(f"Hardware Acceleration     : {agent.laya.execution_provider}")
    print(f"Average Compressed State  : {avg_tokens:.1f} tokens (Hard Ceiling: 400 tokens)")
    print("-" * 70)
    print(f"Data Ingestion Latency    : {avg_ingest:.3f} ms")
    print(f"SMC Extraction Latency    : {avg_smc:.3f} ms")
    print(f"State Compression Latency : {avg_comp:.3f} ms")
    print(f"Laya GPU Inference (P50)  : {avg_infer:.3f} ms (Target: 7 - 33 ms)")
    print(f"Execution Gateway Latency : {avg_exec:.3f} ms")
    print("-" * 70)
    print(f"Mean Total Latency        : {avg_total:.3f} ms")
    print(f"P95 Total Latency         : {p95_total:.3f} ms")
    print(f"P99 Total Latency         : {p99_total:.3f} ms")
    print(f"Min / Max Total Latency   : {min_total:.3f} ms / {max_total:.3f} ms")
    print("=" * 70)

    if avg_total <= 40.0:
        print(f"[PASSED] Ultra-low latency requirement SATISFIED ({avg_total:.2f}ms <= 40ms)")
    else:
        print(f"[WARNING] Latency target exceeded ({avg_total:.2f}ms > 40ms)")

    if max(token_counts) <= 400:
        print(f"[PASSED] Token context budget SATISFIED (Max: {max(token_counts)} tokens <= 400 tokens)")
    else:
        print(f"[WARNING] Context ceiling exceeded (Max: {max(token_counts)} tokens > 400 tokens)")


def main():
    print_banner()
    parser = argparse.ArgumentParser(description="SparkX Terminal Decision Agent")
    parser.add_argument("--benchmark", action="store_true", help="Run latency profiling benchmark")
    parser.add_argument("--iterations", type=int, default=50, help="Benchmark sample count")
    parser.add_argument("--loop", action="store_true", help="Run continuous Antigravity event loop")
    args = parser.parse_args()

    agent = SparkXTradingAgent()

    if args.benchmark or (not args.loop and not args.benchmark):
        # Default to running benchmark demonstration
        asyncio.run(run_latency_benchmark(agent, iterations=args.iterations))
    elif args.loop:
        asyncio.run(agent.run_antigravity_loop())


if __name__ == "__main__":
    main()
