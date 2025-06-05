; hello.asm - Test basic DOS output functions
; Assemble with: nasm -f bin hello.asm -o hello.com

org 100h

section .text
start:
    ; Test function 02h - Write character
    mov ah, 02h
    mov dl, 'H'
    int 21h
    
    mov dl, 'i'
    int 21h
    
    mov dl, 0Dh  ; CR
    int 21h
    
    mov dl, 0Ah  ; LF
    int 21h
    
    ; Test function 09h - Write string
    mov ah, 09h
    mov dx, msg
    int 21h
    
    ; Test prompt detection
    mov ah, 09h
    mov dx, prompt_msg
    int 21h
    
    ; Exit
    mov ah, 4Ch
    mov al, 0
    int 21h

section .data
msg:        db 'Hello from DOS!', 0Dh, 0Ah, '$'
prompt_msg: db 0Dh, 0Ah, '>', '$'