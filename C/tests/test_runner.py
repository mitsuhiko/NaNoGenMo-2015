#!/usr/bin/env python3
"""
Test runner for the DOS emulator
Tests various DOS functionality and pipe communication
"""

import subprocess
import os
import sys
import tempfile
import time
import threading
import queue

class DOSEmulatorTest:
    def __init__(self, emulator_path="../msdos_improved"):
        self.emulator = emulator_path
        self.test_dir = os.path.dirname(os.path.abspath(__file__))
        self.passed = 0
        self.failed = 0
        
    def compile_asm(self, asm_file, output_file):
        """Compile assembly file to COM executable using nasm"""
        try:
            # Try to use nasm to compile
            result = subprocess.run(
                ["nasm", "-f", "bin", asm_file, "-o", output_file],
                capture_output=True,
                text=True
            )
            if result.returncode != 0:
                print(f"Failed to compile {asm_file}: {result.stderr}")
                return False
            return True
        except FileNotFoundError:
            print("NASM not found. Creating pre-compiled test binaries...")
            return self.create_precompiled_binary(asm_file, output_file)
    
    def create_precompiled_binary(self, asm_file, output_file):
        """Create simple pre-compiled COM files for testing"""
        # These are hand-assembled versions of our test programs
        if "hello.asm" in asm_file:
            # MOV AH,02 MOV DL,'H' INT 21 MOV DL,'i' INT 21 etc...
            code = bytes([
                0xB4, 0x02,  # MOV AH, 02h
                0xB2, 0x48,  # MOV DL, 'H'
                0xCD, 0x21,  # INT 21h
                0xB2, 0x69,  # MOV DL, 'i'
                0xCD, 0x21,  # INT 21h
                0xB2, 0x0D,  # MOV DL, 0Dh (CR)
                0xCD, 0x21,  # INT 21h
                0xB2, 0x0A,  # MOV DL, 0Ah (LF)
                0xCD, 0x21,  # INT 21h
                0xB4, 0x4C,  # MOV AH, 4Ch
                0xB0, 0x00,  # MOV AL, 0
                0xCD, 0x21,  # INT 21h
            ])
        elif "simple_exit.asm" in asm_file:
            # Just exit immediately
            code = bytes([
                0xB4, 0x4C,  # MOV AH, 4Ch
                0xB0, 0x00,  # MOV AL, 0
                0xCD, 0x21,  # INT 21h
            ])
        else:
            return False
            
        with open(output_file, 'wb') as f:
            f.write(code)
        return True
    
    def run_test(self, test_name, program, expected_output=None, input_data=None, timeout=5):
        """Run a single test"""
        print(f"\nRunning test: {test_name}")
        
        try:
            # Run the emulator with the test program
            proc = subprocess.Popen(
                [self.emulator, program],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Send input if provided
            stdout, stderr = proc.communicate(input=input_data, timeout=timeout)
            
            # Check if process exited cleanly
            if proc.returncode != 0:
                print(f"  ❌ FAILED: Process exited with code {proc.returncode}")
                print(f"     stderr: {stderr}")
                self.failed += 1
                return False
            
            # Check expected output if provided
            if expected_output and expected_output not in stdout:
                print(f"  ❌ FAILED: Expected output not found")
                print(f"     Expected: {repr(expected_output)}")
                print(f"     Got: {repr(stdout)}")
                self.failed += 1
                return False
            
            print(f"  ✅ PASSED")
            self.passed += 1
            return True
            
        except subprocess.TimeoutExpired:
            print(f"  ❌ FAILED: Test timed out after {timeout} seconds")
            proc.kill()
            self.failed += 1
            return False
        except Exception as e:
            print(f"  ❌ FAILED: {str(e)}")
            self.failed += 1
            return False
    
    def test_pipe_communication(self):
        """Test pipe communication similar to Eliza/Racter setup"""
        print("\nRunning pipe communication test")
        
        # Create a simple echo program
        echo_code = bytes([
            # Print prompt
            0xB4, 0x09,  # MOV AH, 09h
            0xBA, 0x20, 0x01,  # MOV DX, 0120h (offset of prompt string)
            0xCD, 0x21,  # INT 21h
            
            # Read input
            0xB4, 0x01,  # MOV AH, 01h
            0xCD, 0x21,  # INT 21h
            
            # Echo it back
            0xB4, 0x02,  # MOV AH, 02h
            0x88, 0xC2,  # MOV DL, AL
            0xCD, 0x21,  # INT 21h
            
            # Exit
            0xB4, 0x4C,  # MOV AH, 4Ch
            0xB0, 0x00,  # MOV AL, 0
            0xCD, 0x21,  # INT 21h
            
            # Data section at offset 0x120
            *([0x90] * (0x120 - 0x18)),  # Padding
            *b'\r\n>$'  # Prompt
        ])
        
        with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
            f.write(echo_code)
            echo_prog = f.name
        
        try:
            # Test 1: Send input and check echo
            result = self.run_test(
                "Pipe echo test",
                echo_prog,
                expected_output=">",
                input_data="X\n",
                timeout=2
            )
            
            # Test 2: Multiple programs communicating
            # This would simulate the Eliza/Racter setup
            # For now, we just test that the emulator handles pipes correctly
            
        finally:
            os.unlink(echo_prog)
    
    def test_dos_version(self):
        """Test DOS version detection"""
        # Simple program that gets DOS version and exits
        version_code = bytes([
            0xB4, 0x30,  # MOV AH, 30h (Get DOS version)
            0xCD, 0x21,  # INT 21h
            0xB4, 0x4C,  # MOV AH, 4Ch
            0xCD, 0x21,  # INT 21h
        ])
        
        with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
            f.write(version_code)
            prog = f.name
        
        try:
            self.run_test("DOS version test", prog)
        finally:
            os.unlink(prog)
    
    def test_prompt_detection(self):
        """Test Racter-style prompt detection"""
        # Program that outputs CR LF > sequence
        prompt_code = bytes([
            0xB4, 0x02,  # MOV AH, 02h
            0xB2, 0x0D,  # MOV DL, 0Dh (CR)
            0xCD, 0x21,  # INT 21h
            0xB2, 0x0A,  # MOV DL, 0Ah (LF)
            0xCD, 0x21,  # INT 21h
            0xB2, 0x3E,  # MOV DL, '>'
            0xCD, 0x21,  # INT 21h
            0xB4, 0x4C,  # MOV AH, 4Ch
            0xCD, 0x21,  # INT 21h
        ])
        
        with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
            f.write(prompt_code)
            prog = f.name
        
        try:
            self.run_test(
                "Prompt detection test",
                prog,
                expected_output="\r\n>",
                timeout=2
            )
        finally:
            os.unlink(prog)
    
    def run_all_tests(self):
        """Run all tests"""
        print("DOS Emulator Test Suite")
        print("=" * 50)
        
        # Check if emulator exists
        if not os.path.exists(self.emulator):
            print(f"Error: Emulator not found at {self.emulator}")
            print("Please run 'make' first to build the emulator")
            return False
        
        # Test 1: Simple exit
        simple_exit = bytes([
            0xB4, 0x4C,  # MOV AH, 4Ch
            0xB0, 0x00,  # MOV AL, 0
            0xCD, 0x21,  # INT 21h
        ])
        
        with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
            f.write(simple_exit)
            exit_prog = f.name
        
        try:
            self.run_test("Simple exit test", exit_prog)
        finally:
            os.unlink(exit_prog)
        
        # Test 2: DOS version
        self.test_dos_version()
        
        # Test 3: Character output
        char_output = bytes([
            0xB4, 0x02,  # MOV AH, 02h
            0xB2, 0x41,  # MOV DL, 'A'
            0xCD, 0x21,  # INT 21h
            0xB4, 0x4C,  # MOV AH, 4Ch
            0xCD, 0x21,  # INT 21h
        ])
        
        with tempfile.NamedTemporaryFile(suffix='.com', delete=False) as f:
            f.write(char_output)
            char_prog = f.name
        
        try:
            self.run_test("Character output test", char_prog, expected_output="A")
        finally:
            os.unlink(char_prog)
        
        # Test 4: Prompt detection
        self.test_prompt_detection()
        
        # Test 5: Pipe communication
        self.test_pipe_communication()
        
        # Summary
        print("\n" + "=" * 50)
        print(f"Tests passed: {self.passed}")
        print(f"Tests failed: {self.failed}")
        print(f"Total tests: {self.passed + self.failed}")
        
        return self.failed == 0

if __name__ == "__main__":
    tester = DOSEmulatorTest()
    success = tester.run_all_tests()
    sys.exit(0 if success else 1)