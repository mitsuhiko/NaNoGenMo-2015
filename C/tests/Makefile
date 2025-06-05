# Makefile for DOS emulator tests

.PHONY: all test clean help

all: test

test: ../msdos_improved
	@echo "Running DOS emulator tests..."
	./run_tests.sh

../msdos_improved:
	@echo "Building emulator..."
	cd .. && $(MAKE) msdos_improved

# Individual test targets
test-basic:
	@echo "Running basic functionality tests..."
	./run_tests.sh | grep -E "(Test [1-7]|PASSED|FAILED)"

test-communication:
	@echo "Running communication tests..."
	python3 racter_simulator.py

test-stress:
	@echo "Running stress tests..."
	python3 test_runner.py

# Create sample test programs
samples: hello.com echo_test.com fcb_test.com

hello.com: hello.asm
	nasm -f bin hello.asm -o hello.com

echo_test.com: echo_test.asm
	nasm -f bin echo_test.asm -o echo_test.com

fcb_test.com: fcb_test.asm
	nasm -f bin fcb_test.asm -o fcb_test.com

clean:
	rm -f *.com *.o *~
	rm -f TESTFILE.TXT

help:
	@echo "Available targets:"
	@echo "  test           - Run all tests"
	@echo "  test-basic     - Run basic functionality tests"
	@echo "  test-communication - Run pipe communication tests"
	@echo "  test-stress    - Run stress tests"
	@echo "  samples        - Build sample test programs (requires nasm)"
	@echo "  clean          - Clean up test files"
	@echo "  help           - Show this help"