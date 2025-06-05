; echo_test.asm - Test input/output echo behavior
; This simulates Racter's prompt and echo pattern

org 100h

section .text
start:
    ; Print initial prompt
    mov ah, 09h
    mov dx, initial_prompt
    int 21h
    
input_loop:
    ; Read character with echo (function 01h)
    mov ah, 01h
    int 21h
    
    ; Check if it's Enter
    cmp al, 0Dh
    je print_prompt
    
    ; Store character and continue
    jmp input_loop
    
print_prompt:
    ; Print newline
    mov ah, 02h
    mov dl, 0Ah
    int 21h
    
    ; Print response
    mov ah, 09h
    mov dx, response
    int 21h
    
    ; Print the prompt (CR LF >)
    mov ah, 09h
    mov dx, prompt
    int 21h
    
    ; Exit after one round
    mov ah, 4Ch
    xor al, al
    int 21h

section .data
initial_prompt: db 'What is your name? ', '$'
response:       db 'Nice to meet you!', 0Dh, 0Ah, '$'
prompt:         db 0Dh, 0Ah, '>', '$'