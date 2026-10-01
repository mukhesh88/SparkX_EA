"""
Prompt Templates for Laya Decision Engine
Engineered specifically for Laya's 512-token context limit and stateless single-turn evaluation.
Outputs structured probability distributions for Choice, Score, and Noul primitives.
"""

from typing import List, Dict

CHOICE_CLASSES: List[str] = [
    "MARKET_BUY",
    "MARKET_SELL",
    "LIMIT_BUY_ORDER_BLOCK",
    "LIMIT_SELL_ORDER_BLOCK",
    "HOLD"
]

SCORE_RANGE: List[int] = list(range(1, 11))  # 1 to 10

class LayaPromptTemplates:
    """
    Constructs prompt payloads for Laya's 3 decision primitives.
    Total tokens (Compressed State + Prompt Template) is strictly capped under 512 tokens.
    """

    @staticmethod
    def build_choice_prompt(compressed_market_state: str) -> str:
        """
        Choice Primitive: Evaluates SMC confluence and selects trade action.
        Classes: ['MARKET_BUY', 'MARKET_SELL', 'LIMIT_BUY_ORDER_BLOCK', 'LIMIT_SELL_ORDER_BLOCK', 'HOLD']
        Estimated prompt overhead: ~65 tokens.
        """
        return (
            "TASK: CHOICE_SELECTION\n"
            "SYSTEM: You are the SparkX SMC Decision Engine. Evaluate market state confluence.\n"
            f"{compressed_market_state}\n"
            "RULES:\n"
            "1. BUY requires Discount zone, Bullish MSS/BOS, SSL swept or Bull FVG/OB active.\n"
            "2. SELL requires Premium zone, Bearish MSS/BOS, BSL swept or Bear FVG/OB active.\n"
            "3. HOLD if opposing signals, equilibrium, or lack of displacement.\n"
            "CLASSES: [MARKET_BUY, MARKET_SELL, LIMIT_BUY_ORDER_BLOCK, LIMIT_SELL_ORDER_BLOCK, HOLD]\n"
            "OUTPUT_PRIMITIVE: CHOICE"
        )

    @staticmethod
    def build_score_prompt(compressed_market_state: str, intended_action: str) -> str:
        """
        Score Primitive: Grades setup quality from 1 (Chop/Invalid) to 10 (A+ High-probability expansion).
        Estimated prompt overhead: ~55 tokens.
        """
        return (
            "TASK: SETUP_QUALITY_SCORE\n"
            "SYSTEM: Grade setup probability from 1 (Low/Chop) to 10 (A+ ICT/SMC Model).\n"
            f"{compressed_market_state}\n"
            f"EVALUATE_ACTION: {intended_action}\n"
            "CRITERIA: Multi-TF alignment, Liquidity sweep depth, Displacement, FVG freshness, Spread.\n"
            "OUTPUT_PRIMITIVE: SCORE(1-10)"
        )

    @staticmethod
    def build_noul_prompt(compressed_market_state: str, hypothesis: str) -> str:
        """
        Noul Primitive: Binary classification (True / False probability distribution).
        Hypotheses examples:
          - 'Did price sweep Asian session liquidity and reject?'
          - 'Is the current M5 Fair Value Gap valid and untested?'
          - 'Is Market Structure Shift (MSS) confirmed with displacement?'
        Estimated prompt overhead: ~45 tokens.
        """
        return (
            "TASK: NOUL_HYPOTHESIS_CHECK\n"
            "SYSTEM: Evaluate single-predicate truth probability based on SMC market state.\n"
            f"{compressed_market_state}\n"
            f"HYPOTHESIS: {hypothesis}\n"
            "OUTPUT_PRIMITIVE: NOUL(TRUE, FALSE)"
        )

    @staticmethod
    def get_standard_noul_checks(symbol: str) -> Dict[str, str]:
        """Returns standard pre-execution SMC validation hypotheses."""
        return {
            "asian_sweep_confirmed": "Did price sweep Asian session high or low liquidity and cleanly reject back inside?",
            "fvg_valid_untested": "Is there a valid, fresh, and unmitigated M5 Fair Value Gap in the direction of order flow?",
            "mss_confirmed": "Is the recent micro-structure shift confirmed by aggressive displacement candles?"
        }
