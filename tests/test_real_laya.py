"""
Verification test running the real official Laya decision model (convaiinnovations/laya).
"""

import time
import laya

def test_real_laya():
    print("[*] Loading real official Laya agent: convaiinnovations/laya...")
    t0 = time.perf_counter()
    agent = laya.load('convaiinnovations/laya')
    load_time = (time.perf_counter() - t0) * 1000.0
    print(f"[*] Loaded in {load_time:.2f}ms. Device: {agent.device}")

    state = (
        "<MKT_STATE sym='XAUUSD' time='14:15:00Z' px='2363.20' spd='1.2pts' sess='NY_OVERLAP'>\n"
        "BIAS|H1:BULLISH|M15:BOS_BULLISH|M5:MSS_BULLISH\n"
        "DEALING_RANGE|LO:2358.00|HI:2375.00|EQ:2366.50|ZONE:DISCOUNT(30.5%)\n"
        "LIQUIDITY|ASIA_H:2374.00|ASIA_L:2360.00|SWEEP:SSL_SWEPT(-15.0p)|BSL_TGT:2374.00|SSL_TGT:2357.50\n"
        "SMC_ARRAYS|FVG:B:2360.80-2362.50(CE:2361.65)|OB:B:2358.50-2359.80\n"
        "MOMENTUM|ATR:2.20|DISP:T|VOL_EXP:T\n"
        "</MKT_STATE>"
    )

    questions = {
        'action': {
            'type': 'choice',
            'instructions': 'Select trade execution action based on SMC confluence.',
            'criteria': {
                'MARKET_BUY': 'Discount zone, Bullish MSS/BOS, SSL swept, Bullish FVG active',
                'MARKET_SELL': 'Premium zone, Bearish MSS/BOS, BSL swept, Bearish FVG active',
                'LIMIT_BUY_ORDER_BLOCK': 'Discount zone, Bullish Order Block mitigation untested',
                'LIMIT_SELL_ORDER_BLOCK': 'Premium zone, Bearish Order Block mitigation untested',
                'HOLD': 'Equilibrium or lack of displacement'
            }
        },
        'score': {
            'type': 'score',
            'instructions': 'Grade setup quality from 1 to 10.',
            'criteria': [f'Tier {i}' for i in range(1, 11)]
        },
        'asian_sweep_confirmed': {
            'type': 'noul',
            'instructions': 'Did price sweep Asian session high or low liquidity and reject?'
        },
        'fvg_valid_untested': {
            'type': 'noul',
            'instructions': 'Is there an unmitigated Fair Value Gap active in the direction of order flow?'
        },
        'mss_confirmed': {
            'type': 'noul',
            'instructions': 'Is market structure shift confirmed by aggressive displacement?'
        }
    }

    print("[*] Running single-turn parallel forward pass (System 1)...")
    t1 = time.perf_counter()
    decision = agent.system_one(state, questions)
    latency_ms = (time.perf_counter() - t1) * 1000.0

    print(f"\n[REAL LAYA INFERENCE RESULT] (Latency: {latency_ms:.2f}ms):")
    answers = decision['answers']
    
    # Choice Primitive
    choice_ans = answers['action']
    print(f"  CHOICE : {choice_ans['choice']} (Prob: {choice_ans['probabilities'].get(choice_ans['choice'], 0.0):.1%})")
    print(f"           Distribution: {choice_ans['probabilities']}")

    # Score Primitive
    score_ans = answers['score']
    print(f"  SCORE  : {score_ans['score']:.2f} / 10.0")

    # Noul Primitives
    print("  NOUL   :")
    for k in ['asian_sweep_confirmed', 'fvg_valid_untested', 'mss_confirmed']:
        n = answers[k]
        print(f"    - {k}: {n['noul']:.1%} (Confidence: {n['confidence']:.1%})")

    print(f"\n  Token Usage: {decision['usage']}")
    print("\n[SUCCESS] Real Laya model evaluated successfully!")

if __name__ == '__main__':
    test_real_laya()
