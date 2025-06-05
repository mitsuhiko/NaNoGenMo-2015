# DOS Emulator Test Suite

This directory contains comprehensive tests for the `msdos` DOS emulator.

## Overview

The test suite validates:
- Basic DOS INT 21h function calls
- Character input/output handling
- Pipe communication (critical for Eliza/Racter communication)
- Prompt detection (Racter's "CR LF >" sequence)
- Non-blocking I/O behavior
- FCB file operations
- Error handling and edge cases

## Test Categories

### 1. Basic Functionality Tests (`run_tests.sh`)
- **Simple Exit**: Tests program termination (INT 21h AH=4Ch)
- **Character Output**: Tests character output (INT 21h AH=02h)
- **Multiple Characters**: Tests sequential character output
- **Prompt Detection**: Tests the critical "CR LF >" sequence detection
- **DOS Version**: Tests DOS version call (INT 21h AH=30h)
- **Non-blocking Input**: Tests input that doesn't hang when no data available
- **Pipe Input**: Tests reading from pipes
- **Debug Mode**: Tests debug output functionality

### 2. Communication Tests (`racter_simulator.py`)
- **Mock Racter**: Simulates Racter's I/O patterns
- **Mock Eliza**: Simulates Eliza's response patterns
- **Pipe Communication**: Tests bidirectional pipe communication
- **Stress Testing**: Rapid I/O operations

### 3. Comprehensive Tests (`test_runner.py`)
- **Timeout Handling**: Ensures programs don't hang indefinitely
- **Expected Output Validation**: Verifies correct output content
- **Error Code Checking**: Validates proper exit codes
- **Multiple Test Scenarios**: Various DOS program patterns

## Running Tests

### Quick Start
```bash
# Run all tests
make test

# Or run directly
./run_tests.sh
```

### Individual Test Categories
```bash
# Basic functionality only
make test-basic

# Communication tests
make test-communication

# Stress tests
make test-stress
```

### Python Tests
```bash
# Comprehensive test suite
python3 test_runner.py

# Communication simulation
python3 racter_simulator.py
```

## Test Programs

The test suite includes several types of test programs:

### Hand-assembled COM Files
For maximum compatibility, many tests use hand-assembled machine code:
- No external assembler dependencies
- Known working instruction sequences
- Minimal program size
- Direct control over every byte

### Assembly Source Files
Example assembly programs that can be assembled with NASM:
- `hello.asm` - Basic character output
- `echo_test.asm` - Input/output echo pattern
- `fcb_test.asm` - File operations using FCB

### Generated Test Programs
Python scripts create test programs dynamically:
- Mock Racter/Eliza simulators
- Stress test programs
- Edge case scenarios

## Test Validation

### Output Validation
Tests validate:
- Expected character sequences in output
- Proper prompt detection ("CR LF >")
- Correct program exit codes
- No hanging or infinite loops

### Timing Tests
- Programs must complete within timeout periods
- Non-blocking I/O must not cause hangs
- Pipe communication must be responsive

### Error Handling
- Invalid program files
- Missing input data
- Malformed DOS calls

## Creating New Tests

### Adding a Basic Test
1. Create a small COM program (hand-assembled or from ASM)
2. Add test case to `run_tests.sh`
3. Define expected output and behavior

Example:
```bash
echo "Test N: Description"
printf '\xB4\x02\xB2\x41\xCD\x21\xB4\x4C\xCD\x21' > test.com
output=$(timeout 5 ../msdos test.com 2>/dev/null || true)
if [[ "$output" == *"A"* ]]; then
    echo "✅ PASSED"
else
    echo "❌ FAILED"
fi
rm -f test.com
```

### Adding a Python Test
Add to `test_runner.py` or create new Python script:
```python
def test_new_feature(self):
    code = bytes([...])  # Machine code
    with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
        f.write(code)
        prog = f.name
    
    try:
        self.run_test("New feature test", prog, expected_output="Expected")
    finally:
        os.unlink(prog)
```

## Understanding Test Results

### Success Indicators
- ✅ PASSED - Test completed successfully
- Exit code 0 from test runner
- Expected output matches actual output

### Failure Indicators
- ❌ FAILED - Test failed
- Timeout errors (program hung)
- Unexpected output
- Non-zero exit codes

### Debug Information
Use debug mode to see detailed DOS function calls:
```bash
../msdos test_program.com -d
```

## Common Issues

### Test Timeouts
- Indicates program is hanging or stuck in loop
- Check for proper input handling
- Verify prompt detection logic

### Output Mismatches
- Character encoding issues (CR/LF vs LF)
- Missing prompt sequences
- Extra debug output

### Pipe Communication Failures
- Non-blocking I/O not working
- Buffer management issues
- Timing problems between reader/writer

## Integration with Main Project

These tests validate that `msdos` can properly handle:
1. Racter's DOS function calls
2. Pipe communication with couch.lua
3. Prompt detection for conversation flow
4. Non-hanging I/O operations

The tests ensure the emulator works correctly in the Eliza/Racter conversation setup without the deadlocks experienced with the original implementation.