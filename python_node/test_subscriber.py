"""
Diagnostic Python ZeroMQ Subscriber
Tests reception of compressed SMC market frames from python_node/publisher.py.
"""

import zmq
import json
import time

def test_subscriber(connect_address="tcp://127.0.0.1:5555", timeout_sec=5.0):
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(connect_address)
    socket.setsockopt_string(zmq.SUBSCRIBE, "SMC_MARKET_STATE")
    socket.setsockopt(zmq.RCVTIMEO, int(timeout_sec * 1000))

    print(f"Subscribed to {connect_address} on topic 'SMC_MARKET_STATE'. Waiting for frames...")
    try:
        topic, message = socket.recv_multipart()
        data = json.loads(message.decode('utf-8'))
        print("\n[SUCCESS] Received Market Frame:")
        print(f"  Symbol           : {data['symbol']}")
        print(f"  Price            : {data['price']}")
        print(f"  Spread           : {data['spread']} pts")
        print(f"  Token Count      : {data['token_count']} (Limit: 512)")
        print(f"  Compressed State :\n{data['compressed_state']}")
        return True
    except zmq.Again:
        print("[TIMEOUT] No message received within timeout window.")
        return False
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    test_subscriber()
