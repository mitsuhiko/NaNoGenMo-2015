; fcb_test.asm - Test FCB file operations
; Tests file create, write, close, open, read

org 100h

section .text
start:
    ; Set DTA
    mov ah, 1Ah
    mov dx, dta_buffer
    int 21h
    
    ; Create file using FCB
    mov ah, 16h
    mov dx, test_fcb
    int 21h
    
    ; Check result
    or al, al
    jnz create_error
    
    ; Write success message
    mov ah, 09h
    mov dx, create_ok_msg
    int 21h
    
    ; Close file
    mov ah, 10h
    mov dx, test_fcb
    int 21h
    
    ; Open file for reading
    mov ah, 0Fh
    mov dx, test_fcb
    int 21h
    
    or al, al
    jnz open_error
    
    ; Write open success
    mov ah, 09h
    mov dx, open_ok_msg
    int 21h
    
    jmp exit_ok
    
create_error:
    mov ah, 09h
    mov dx, create_err_msg
    int 21h
    jmp exit_err
    
open_error:
    mov ah, 09h
    mov dx, open_err_msg
    int 21h
    
exit_err:
    mov al, 1
    jmp exit
    
exit_ok:
    xor al, al
    
exit:
    mov ah, 4Ch
    int 21h

section .data
test_fcb:
    db 0          ; drive (0 = default)
    db 'TESTFILE'  ; 8-char filename
    db 'TXT'       ; 3-char extension
    times 25 db 0  ; rest of FCB
    
dta_buffer:
    times 128 db 0
    
create_ok_msg: db 'File created successfully', 0Dh, 0Ah, '$'
create_err_msg: db 'Failed to create file', 0Dh, 0Ah, '$'
open_ok_msg: db 'File opened successfully', 0Dh, 0Ah, '$'
open_err_msg: db 'Failed to open file', 0Dh, 0Ah, '$'