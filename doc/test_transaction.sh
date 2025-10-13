#!/bin/bash

echo "Testing POST /transaction endpoint..."
echo

# Test 1: Valid transaction
echo "Test 1: Creating a valid transaction"
curl -X POST http://localhost:28443/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
    "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
    "data": "SGVsbG8gQmxvY2tjaGFpbiBkYXRhIQ==",
    "fee": 0.00012
  }' 2>/dev/null
echo
echo

# Test 2: Missing required field
echo "Test 2: Missing 'to' field (should error)"
curl -X POST http://localhost:28443/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "from": "test_address",
    "data": "VGVzdA=="
  }' 2>/dev/null
echo
echo

# Test 3: Check mempool
echo "Test 3: Checking mempool size"
curl -s http://localhost:28443/chain
echo
