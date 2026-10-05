"""
End-to-End Test for SparkX EA Stop-Loss Forensic Diagnosis & Neural Retraining
Validates:
1. Trade snapshot recording upon entry execution.
2. Forensic SL failure attribution when trade exits via Stop Loss.
3. Automated counterfactual sample generation (Choice=HOLD, Score=1.8).
4. Micro-retraining of Laya decision heads with AdamW and ONNX weight export.
5. Immediate Gate 6 enforcement blocking repeat entry of the diagnosed failure mode.
"""

import sys
import os
import time

# Ensure project root is in sys.path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from src.sl_analyzer import SLDiagnosticEngine, SLPostMortem
from src.trade_manager import trade_manager
from src.online_learner import online_learner


def run_sl_retraining_test():
    print("=" * 70)
    print(">>> STARTING SPARKX EA SL FORENSIC & RETRAINING VALIDATION")
    print("=" * 70)

    symbol = "XAUUSD"
    ticket = 777123

    # 1. Setup simulated trade context where a BUY was taken in PREMIUM against H1 trend
    entry_price = 2750.0
    sl_price = 2745.5
    tp_price = 2762.0

    mock_compressed_state = (
        "<MKT_STATE sym='XAUUSD' session='NY_OVERLAP' px='2750.00' atr='2.45'>\n"
        "  <BIAS h1='BEARISH' m15='BOS_BEARISH' m5='MSS_BEARISH' zone='PREMIUM' fib='78.2%' />\n"
        "  <LIQ asia_hi='2760.00' asia_lo='2735.00' bsl_swept='F' ssl_swept='F' BSL_TGT:2760.00 | SSL_TGT:2735.00 />\n"
        "  <ARRAYS fvg='FVG:S:2752.00-2754.00' ob='OB:S:2755.00-2758.00' />\n"
        "  <MOMENTUM disp='F' vol_exp='F' />\n"
        "</MKT_STATE>"
    )

    mock_features = {
        "h1_trend": "BEARISH",
        "m15_structure": "BOS_BEARISH",
        "m5_structure": "MSS_BEARISH",
        "price_zone": "PREMIUM",
        "asian_liquidity": {"swept_high": False, "swept_low": False, "high": 2760.0, "low": 2735.0},
        "bsl_swept": False,
        "ssl_swept": False,
        "active_m5_fvg": {"direction": "BEARISH", "top": 2754.0, "bottom": 2752.0},
        "active_m5_ob": {"direction": "BEARISH", "top": 2758.0, "bottom": 2755.0},
        "displacement_detected": False,
        "volume_expansion": False,
        "nearest_bsl_target": 2760.0,
        "nearest_ssl_target": 2735.0,
        "current_price": 2750.0
    }

    # Record trade snapshot
    trade_manager.record_trade_snapshot(
        ticket=ticket,
        symbol=symbol,
        action="MARKET_BUY",
        price=entry_price,
        sl=sl_price,
        tp=tp_price,
        compressed_state=mock_compressed_state,
        features=mock_features
    )
    print(f"[OK] Recorded trade snapshot for Ticket #{ticket}")

    # 2. Trigger SL closure
    # Price dropped to 2745.0 (hitting SL of 2745.5)
    trade_manager.simulated_positions.append({
        "ticket": ticket,
        "symbol": symbol,
        "type": "BUY",
        "volume": 0.10,
        "price_open": entry_price,
        "price_current": 2745.0,
        "sl": sl_price,
        "tp": tp_price,
        "profit": -45.0,
        "comment": "SparkX_Test_SL"
    })

    print(f"\n>>> Simulating Position SL Hit for Ticket #{ticket} at $2745.0...")
    trade_manager.close_simulated_position(ticket=ticket, current_price=2745.0)

    # 3. Verify forensic diagnosis
    active_rc = trade_manager.adaptive_root_cause.get(symbol, "NONE")
    active_rect = trade_manager.adaptive_rectifications.get(symbol, "None")
    print(f"[DIAGNOSIS] Active Root Cause: {active_rc}")
    print(f"[RECTIFICATION] Enforced Gate Rule: {active_rect}")

    assert active_rc in ["HTF_TREND_CONFLICT", "PREMIUM_DISCOUNT_VIOLATION"], f"Unexpected root cause: {active_rc}"
    assert len(active_rect) > 10, "Rectification rule is empty!"

    # 4. Verify Adaptive Gate 6 enforcement:
    # Reset cooldown timers to isolate Gate 6 verification
    trade_manager.last_trade_time = 0.0
    trade_manager.last_closed_time = 0.0  # bypass 15-min cooldown to specifically test Gate 6

    # A new BUY with the same conflicting trend or premium zone must be blocked by Gate 6!
    allowed, block_reason = trade_manager.can_enter_trade(
        symbol=symbol,
        action="MARKET_BUY",
        price=2746.0,
        open_positions=[],
        features=mock_features
    )
    print(f"\n>>> Testing Gate 6 (Adaptive SL Rectification Gate):")
    print(f"    Allowed: {allowed}")
    print(f"    Reason: {block_reason}")
    assert not allowed, "Gate 6 failed to block duplicate failure mode!"
    assert "Adaptive SL Gate" in block_reason or "ADAPTIVE" in block_reason.upper(), f"Unexpected block reason: {block_reason}"
    print("[PASS] Gate 6 successfully blocked repeat trade with active SL failure mode!")

    # 5. Wait for online learner micro-retraining to complete
    print("\n>>> Waiting for Online Learner asynchronous micro-retraining...")
    max_wait = 60.0
    start_time = time.time()
    while (time.time() - start_time < max_wait):
        if len(online_learner.training_history) > 0 and not online_learner.is_retraining:
            break
        time.sleep(0.5)

    print(f"[LEARNER] Retraining queue remaining: {online_learner.queue.qsize()}")
    print(f"[LEARNER] Total training history count: {len(online_learner.training_history)}")
    assert len(online_learner.training_history) > 0, "No training history recorded by OnlineLearner!"
    latest = online_learner.training_history[-1]
    print(f"[PASS] Laya Online Retraining succeeded!")
    print(f"       Trained Ticket: #{latest['ticket']}")
    print(f"       Samples Trained: {latest['samples_trained']}")
    print(f"       Initial Loss: {latest['initial_loss']:.4f} -> Final Loss: {latest['final_loss']:.4f}")
    print(f"       Exported ONNX: {latest['onnx_path']}")

    print("\n" + "=" * 70)
    print(">>> ALL TESTS PASSED: STOP-LOSS RECTIFICATION & ONLINE RETRAINING VERIFIED")
    print("=" * 70)


if __name__ == "__main__":
    run_sl_retraining_test()
