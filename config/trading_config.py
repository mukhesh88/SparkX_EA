"""
SparkX EA - Trading Configuration
Configures assets, risk rules, SMC parameters, and Laya inference settings.
"""

from dataclasses import dataclass, field
from typing import List, Dict

@dataclass(frozen=True)
class SMCConfig:
    """Smart Money Concepts (SMC) calculation parameters."""
    asian_session_start_utc: int = 0    # 00:00 UTC
    asian_session_end_utc: int = 8      # 08:00 UTC
    london_session_start_utc: int = 8   # 08:00 UTC
    london_session_end_utc: int = 16    # 16:30 UTC
    ny_session_start_utc: int = 13      # 13:00 UTC
    ny_session_end_utc: int = 21        # 21:00 UTC
    
    # Swing high/low fractal lookback (bars on each side)
    swing_lookback_m5: int = 3
    swing_lookback_m15: int = 5
    swing_lookback_h1: int = 5
    
    # Minimum FVG gap threshold in points
    fvg_min_points_xau: float = 0.50     # $0.50 in Gold
    fvg_min_points_btc: float = 30.0     # $30 in Bitcoin
    
    # OB displacement requirement (multiple of ATR)
    ob_displacement_atr_mult: float = 1.5

@dataclass(frozen=True)
class RiskConfig:
    """Quantitative risk management and execution constraints."""
    risk_per_trade_pct: float = 0.01       # 1% equity risk per trade
    max_open_trades_per_symbol: int = 1
    max_daily_drawdown_pct: float = 0.04   # 4% hard daily stop
    min_risk_reward_ratio: float = 2.0     # Minimum 1:2 R:R
    max_spread_points_xau: float = 25.0    # 25 points ($0.25 spread)
    max_spread_points_btc: float = 50.0    # 50 points ($50 spread)
    slippage_points: int = 10

@dataclass(frozen=True)
class LayaConfig:
    """Laya inference model configuration."""
    model_name: str = "laya-zero-shot-v1"
    onnx_model_path: str = "models/laya.onnx"
    tokenizer_path: str = "models/tokenizer"
    max_context_tokens: int = 512
    max_compressed_state_tokens: int = 380  # Hard ceiling for state payload
    target_latency_ms: float = 20.0
    
    # Execution thresholds
    min_choice_confidence: float = 0.85     # Laya overconfidence filter (>85%)
    min_setup_score: float = 7.0            # Quality grade > 7/10
    min_noul_validation: float = 0.70       # Binary condition confirmation
    
    # Hardware acceleration
    execution_providers: List[str] = field(default_factory=lambda: [
        "CUDAExecutionProvider",
        "DmlExecutionProvider",  # DirectML on Windows (AMD/Intel/NVIDIA)
        "CPUExecutionProvider"
    ])
    gpu_device_id: int = 0

@dataclass(frozen=True)
class SystemConfig:
    """SparkX EA master configuration."""
    symbols: List[str] = field(default_factory=lambda: ["XAUUSD"])
    timeframes: List[str] = field(default_factory=lambda: ["H1", "M15", "M5"])
    poll_interval_sec: float = 1.0  # M5 bar / high-resolution tick polling
    smc: SMCConfig = field(default_factory=SMCConfig)
    risk: RiskConfig = field(default_factory=RiskConfig)
    laya: LayaConfig = field(default_factory=LayaConfig)
