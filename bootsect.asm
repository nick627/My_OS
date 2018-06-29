.code16
.intel_syntax noprefix
.global _start

_start:
	mov ax, cs
	mov ds, ax
	mov ss, ax
	mov sp, _start

	mov ah, 0x0e
	mov al, 'H'
	int 0x10
	mov al, 'e'
	int 0x10
	mov al, 'l'
	int 0x10
	int 0x10
	mov al, 'o'
	int 0x10

	mov bx, offset loading_str
	call puts

	mov ax, 03	# clear monitor
	int 0x10

downloader:
	mov edi, 0xb8000
	mov esi, offset hello_str
	call video_puts

	mov edi, 0xb80A0
	mov esi, offset str_bootsect
	call video_puts

	mov ah, 0x00
	int 0x16

	cmp ah, 28	# enter
	je load_kernel

	xor ch, ch	# ch часть cx
	mov cl, al	
	sub cl, 97	# Индекс в алфавите	
	mov bx, offset str_bootsect
	add bx, cx	# bx >>

	mov cl, [bx]
	cmp cl, '_'	# Если символ уже введен
	je paste_ret
	mov al, '_'	# Если символ уже введен
	mov [bx], al

	jmp downloader

paste_ret:	# Если символ не введен
	mov [bx], al
	jmp downloader

load_kernel:
	mov ax, 0x1000
	mov es, ax
	mov bx, 0
	mov ah, 0x02
	mov dl, 1	#
	mov dh, 0	#golovka
	mov ch, 0	#dorohka
	mov cl, 1	#sektor
	mov al, 18	#kol-vo sektorov
	int 0x13

	mov ax, 0x1240
	mov es, ax
	mov bx, 0
	mov ah, 0x02
	mov dl, 1
	mov dh, 1
	mov ch, 0
	mov cl, 1
	mov al, 18
	int 0x13

	cli	# Загрузка размера и адреса таблицы дескрипторов
	lgdt gdt_info	# Для GNU assembler должно быть "lgdt gdt_info"
	# Включение адресной линии А20
	in al, 0x92
	or al, 2
	out 0x92, al
	# Установка бита PE регистра CR0 - процессор перейдет в защищенный режим
	mov eax, cr0
	or al, 1
	mov cr0, eax

	jmp 0x8:protected_mode	# "Дальний" переход для загрузки корректной

puts:
	mov al, [bx]
	cmp al, 0
	je end_puts
	mov ah, 0x0e
	int 0x10
	add bx, 1
	jmp puts

end_puts:
	ret

video_puts:
	mov al, [esi]
	test al, al
	jz video_puts_end
	mov ah, 0x07	# is lightgrey-on-black, 0x1F is white-on-blue
	mov [edi], al
	mov [edi+1], ah
	add edi, 2
	add esi, 1
	jmp video_puts

video_puts_end:
	ret

loading_str:
	.asciz "Loading...\n"

hello_str:
	.asciz "Enter the letters you want to see in the dictionary"

str_bootsect:
	.asciz "__________________________"

gdt:
	.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
	.byte 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00
gdt_info:
	.word gdt_info - gdt
	.word gdt, 0

.code32
protected_mode:
	# Загрузка селекторов сегментов для стека и данных в регистры
	mov ax, 0x10 # Используется дескриптор с номером 2 в GDT
	mov es, ax
	mov ds, ax
	mov ss, ax
	# Передача управления загруженному ядру
	call 0x10000

.zero (512-($-_start)-2)
.byte 0x55, 0xaa
