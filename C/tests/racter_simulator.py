#!/usr/bin/env python3
"""
Racter simulator for testing pipe communication
This creates a mock Racter that follows the same I/O patterns
"""

import subprocess
import sys
import time
import threading
import select
import os

def create_mock_racter():
    """Create a simple COM program that mimics Racter's behavior"""
    # This program:
    # 1. Prints a greeting
    # 2. Waits for input
    # 3. Echoes some response
    # 4. Prints the prompt (CR LF >)
    # 5. Repeats a few times
    
    code = []
    
    # Print greeting
    code.extend([
        0xB4, 0x09,  # MOV AH, 09h
        0xBA, 0x50, 0x01,  # MOV DX, offset greeting
        0xCD, 0x21,  # INT 21h
    ])
    
    # Main loop (simplified - just 3 iterations)
    for i in range(3):
        # Read input until CR
        code.extend([
            0xB4, 0x01,  # MOV AH, 01h (read char with echo)
            0xCD, 0x21,  # INT 21h
            0x3C, 0x0D,  # CMP AL, 0Dh
            0x75, 0xF9,  # JNZ (back to read)
            
            # Print response
            0xB4, 0x09,  # MOV AH, 09h
            0xBA, 0x70, 0x01,  # MOV DX, offset response
            0xCD, 0x21,  # INT 21h
            
            # Print prompt
            0xB4, 0x09,  # MOV AH, 09h
            0xBA, 0x90, 0x01,  # MOV DX, offset prompt
            0xCD, 0x21,  # INT 21h
        ])
    
    # Exit
    code.extend([
        0xB4, 0x4C,  # MOV AH, 4Ch
        0xB0, 0x00,  # MOV AL, 0
        0xCD, 0x21,  # INT 21h
    ])
    
    # Pad to data section
    while len(code) < 0x150:
        code.append(0x90)  # NOP
    
    # Data section
    greeting = b"Hello, I'm Mock Racter.\r\n$"
    response = b"That's interesting.\r\n$"
    prompt = b"\r\n>$"
    
    # Pad to specific offsets
    code.extend(greeting + b'\x00' * (0x170 - 0x150 - len(greeting)))
    code.extend(response + b'\x00' * (0x190 - 0x170 - len(response)))
    code.extend(prompt)
    
    return bytes(code)

def create_mock_eliza():
    """Create a simple program that responds like Eliza"""
    code = []
    
    # Print greeting
    code.extend([
        0xB4, 0x09,  # MOV AH, 09h
        0xBA, 0x40, 0x01,  # MOV DX, offset greeting
        0xCD, 0x21,  # INT 21h
    ])
    
    # Main loop
    for i in range(3):
        # Read input until newline
        code.extend([
            0xB4, 0x01,  # MOV AH, 01h
            0xCD, 0x21,  # INT 21h
            0x3C, 0x0A,  # CMP AL, 0Ah (LF)
            0x75, 0xF9,  # JNZ (back to read)
            
            # Print response
            0xB4, 0x09,  # MOV AH, 09h
            0xBA, 0x60, 0x01,  # MOV DX, offset response
            0xCD, 0x21,  # INT 21h
        ])
    
    # Exit
    code.extend([
        0xB4, 0x4C,  # MOV AH, 4Ch
        0xCD, 0x21,  # INT 21h
    ])
    
    # Pad and add data
    while len(code) < 0x140:
        code.append(0x90)
    
    greeting = b"How do you do ... please state your problem.\r\n$"
    response = b"Tell me more about that.\r\n$"
    
    code.extend(greeting + b'\x00' * (0x160 - 0x140 - len(greeting)))
    code.extend(response)
    
    return bytes(code)

def test_emulator_stress():
    """Stress test the emulator with rapid I/O"""
    print("Creating stress test program...")
    
    # Program that rapidly outputs characters and reads input
    code = []
    
    # Output 100 characters rapidly
    for i in range(100):
        code.extend([
            0xB4, 0x02,  # MOV AH, 02h
            0xB2, 0x41 + (i % 26),  # MOV DL, 'A' + i
            0xCD, 0x21,  # INT 21h
        ])
    
    # Try to read some input
    code.extend([
        0xB4, 0x06,  # MOV AH, 06h (Direct console I/O)
        0xB2, 0xFF,  # MOV DL, 0FFh (input)
        0xCD, 0x21,  # INT 21h
    ])
    
    # Exit
    code.extend([
        0xB4, 0x4C,  # MOV AH, 4Ch
        0xCD, 0x21,  # INT 21h
    ])
    
    return bytes(code)

def run_pipe_test():
    """Test pipe communication between two programs through the emulator"""
    print("Testing pipe communication...")
    
    # Create temporary files
    import tempfile
    
    with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f1:
        f1.write(create_mock_racter())
        racter_prog = f1.name
    
    with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f2:
        f2.write(create_mock_eliza())
        eliza_prog = f2.name
    
    try:
        # Test 1: Run Racter alone
        print("Testing Racter simulator...")
        racter_proc = subprocess.Popen(
            ["../msdos_improved", racter_prog],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Send some input
        stdout, stderr = racter_proc.communicate(
            input="Test input\nAnother line\nThird line\n",
            timeout=5
        )
        
        print(f"Racter output: {repr(stdout)}")
        if "\r\n>" in stdout:
            print("✅ Racter prompt detection working")
        else:
            print("❌ Racter prompt detection failed")
        
        # Test 2: Run Eliza alone
        print("\nTesting Eliza simulator...")
        eliza_proc = subprocess.Popen(
            ["../msdos_improved", eliza_prog],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        stdout, stderr = eliza_proc.communicate(
            input="I feel sad\nI am worried\nI need help\n",
            timeout=5
        )
        
        print(f"Eliza output: {repr(stdout)}")
        
    except Exception as e:
        print(f"Error in pipe test: {e}")
    finally:
        os.unlink(racter_prog)
        os.unlink(eliza_prog)

def run_stress_test():
    """Run stress test"""
    print("Running stress test...")
    
    import tempfile
    
    with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
        f.write(test_emulator_stress())
        stress_prog = f.name
    
    try:
        proc = subprocess.Popen(
            ["../msdos_improved", stress_prog],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        stdout, stderr = proc.communicate(timeout=10)
        
        if len(stdout) >= 100:  # Should have output 100 characters
            print("✅ Stress test passed")
        else:
            print(f"❌ Stress test failed: only got {len(stdout)} characters")
            
    except Exception as e:
        print(f"Stress test error: {e}")
    finally:
        os.unlink(stress_prog)

if __name__ == "__main__":
    print("DOS Emulator Communication Tests")
    print("=" * 40)
    
    run_pipe_test()
    print()
    run_stress_test()
    print("\nTests complete!")