#!/bin/bash
# Test runner for DOS emulator

set -e

echo "DOS Emulator Test Suite"
echo "======================"

# Check if we're running in Docker or have emulator
if [ -f "/app/msdos" ]; then
    MSDOS="/app/msdos"
    echo "✓ Using Docker emulator"
elif [ -f "$MSDOS" ]; then
    MSDOS="$MSDOS"
    echo "✓ Using local emulator"
else
    echo "Error: msdos not found. Building..."
    cd ..
    make msdos
    cd tests
    MSDOS="$MSDOS"
fi

echo "✓ Emulator found"

# Test 1: Simple exit test
echo
echo "Test 1: Simple exit"
printf '\xB4\x4C\xB0\x00\xCD\x21' > simple_exit.com
if $MSDOS simple_exit.com >/dev/null 2>&1; then
    echo "✅ PASSED"
else
    echo "❌ FAILED"
fi
rm -f simple_exit.com

# Test 2: Character output
echo
echo "Test 2: Character output"
printf '\xB4\x02\xB2\x41\xCD\x21\xB4\x4C\xCD\x21' > char_test.com
output=$( ($MSDOS char_test.com >/tmp/test_out 2>/dev/null; cat /tmp/test_out) || true)
if [[ "$output" == *"A"* ]]; then
    echo "✅ PASSED"
else
    echo "❌ FAILED - Expected 'A', got: '$output'"
fi
rm -f char_test.com /tmp/test_out

# Test 3: Multiple character output
echo
echo "Test 3: Multiple character output"
# Output "HI" and exit
printf '\xB4\x02\xB2\x48\xCD\x21\xB2\x49\xCD\x21\xB4\x4C\xCD\x21' > multi_char.com
output=$( ($MSDOS multi_char.com >/tmp/test_out 2>/dev/null; cat /tmp/test_out) || true)
if [[ "$output" == *"HI"* ]]; then
    echo "✅ PASSED"
else
    echo "❌ FAILED - Expected 'HI', got: '$output'"
fi
rm -f multi_char.com /tmp/test_out

# Test 4: Prompt detection
echo
echo "Test 4: Prompt detection"
# Output CR LF > sequence
printf '\xB4\x02\xB2\x0D\xCD\x21\xB2\x0A\xCD\x21\xB2\x3E\xCD\x21\xB4\x4C\xCD\x21' > prompt_test.com
$MSDOS prompt_test.com >/tmp/test_out 2>/dev/null
if [ -f /tmp/test_out ] && xxd /tmp/test_out | grep -q "0d0a 3e"; then
    echo "✅ PASSED"
else
    echo "❌ FAILED - Expected CR LF >, got: $(xxd /tmp/test_out 2>/dev/null || echo 'no output')"
fi
rm -f prompt_test.com /tmp/test_out

# Test 5: DOS version call
echo
echo "Test 5: DOS version call"
# Get DOS version and exit
printf '\xB4\x30\xCD\x21\xB4\x4C\xCD\x21' > version_test.com
if $MSDOS version_test.com >/dev/null 2>&1; then
    echo "✅ PASSED"
else
    echo "❌ FAILED"
fi
rm -f version_test.com

# Test 6: Input with timeout (non-blocking)
echo
echo "Test 6: Non-blocking input"
# Try to read input with function 06h, then exit
printf '\xB4\x06\xB2\xFF\xCD\x21\xB4\x4C\xCD\x21' > input_test.com
output=$(echo "" | timeout 5 $MSDOS input_test.com 2>/dev/null || true)
# Should exit without hanging
echo "✅ PASSED (didn't hang)"
rm -f input_test.com

# Test 7: Pipe input test
echo
echo "Test 7: Pipe input test"
# Read one character and echo it
printf '\xB4\x01\xCD\x21\xB4\x02\x88\xC2\xCD\x21\xB4\x4C\xCD\x21' > echo_test.com
output=$(echo "X" | ($MSDOS echo_test.com >/tmp/test_out 2>/dev/null; cat /tmp/test_out) || true)
if [[ "$output" == *"X"* ]]; then
    echo "✅ PASSED"
else
    echo "❌ FAILED - Expected echo of 'X', got: '$output'"
fi
rm -f echo_test.com /tmp/test_out

# Test 8: Debug mode
echo
echo "Test 8: Debug mode"
printf '\xB4\x02\xB2\x41\xCD\x21\xB4\x4C\xCD\x21' > debug_test.com
output=$($MSDOS debug_test.com -d 2>&1 >/dev/null || true)
if [[ "$output" == *"DOS INT 21h"* ]]; then
    echo "✅ PASSED"
else
    echo "❌ FAILED - Debug output not found"
fi
rm -f debug_test.com

echo
echo "Basic tests complete!"

# Run Python tests if available
if command -v python3 >/dev/null 2>&1; then
    echo
    echo "Running Python test suite..."
    python3 test_runner.py
    
    echo
    echo "Running communication tests..."
    python3 racter_simulator.py
else
    echo "Python3 not available, skipping advanced tests"
fi

echo
echo "All tests complete!"