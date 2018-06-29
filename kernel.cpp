// Эта инструкция обязательно должна быть первой, т.к. этот код компилируется в бинарный,
// и загрузчик передает управление по адресу первой инструкции бинарного образа ядра ОС.
__asm("jmp kmain");
#define VIDEO_BUF_PTR (0xb8000)
#define PIC1_PORT (0x20)

// Базовый порт управления курсором текстового экрана. Подходит для большинства, но может отличаться в других BIOS и в общем случае адрес должен быть прочитан из BIOS data area.
#define CURSOR_PORT (0x3D4)
#define VIDEO_WIDTH (80) // Ширина текстового экрана

#define IDT_TYPE_INTR (0x0E)
#define IDT_TYPE_TRAP (0x0F)
// Селектор секции кода, установленный загрузчиком ОС
#define GDT_CS (0x8)

#define ENTER 28
#define BACKSPACE 14
#define SPACE 57

//#define DEADSTOLB 40
#define DEADLINE 25

#define SIZE_COM 40
#define SIZE_BUF 30

#define INT_DIGITS 20

unsigned int num_line = 0;
int stolb = 2;
char key_com[SIZE_COM] = { 0 };
char key_buf[SIZE_BUF] = { 0 };
unsigned int k_c = 0, k_b = 0;
int flag_buf = 0;

char str_downloader[26] = { 0 };

unsigned char scancodes[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

char spec_copy[20] = { 0 };

const char* sysmsg_entry = "# ";
const char* sysmsg_unknown = "Error: command not recognized";

const char* sysmsg_help = "help\n";
const char* sysmsg_info = "info\n";
const char* sysmsg_dictinfo = "dictinfo\n";
const char* sysmsg_shutdown = "shutdown\n";
const char* sysmsg_translate = "translate ";
const char* sysmsg_wordstat = "wordstat ";

const char* sysmsg_emptystr = "";

// Структура описывает данные об обработчике прерывания
struct idt_entry
{
	unsigned short base_lo;// Младшие биты адреса обработчика
	unsigned short segm_sel;// Селектор сегмента кода
	unsigned char always0;// Этот байт всегда 0
	unsigned char flags;// Флаги тип. Флаги: P, DPL, Типы - это константы - 	IDT_TYPE...
	unsigned short base_hi;// Старшие биты адреса обработчика
} __attribute__((packed)); // Выравнивание запрещено

// Структура, адрес которой передается как аргумент команды lidt
struct idt_ptr
{
	unsigned short limit;
	unsigned int base;
} __attribute__((packed)); // Выравнивание запрещено

struct idt_entry g_idt[256]; // Реальная таблица IDT
struct idt_ptr g_idtp;
// Описатель таблицы для команды lidt

// Пустой обработчик прерываний. Другие обработчики могут быть реализованы по этому шаблону
void default_intr_handler()
{
	asm("pusha");
	// ... (реализация обработки)
	asm("popa; leave; iret");
}

typedef void (*intr_handler)();

void intr_reg_handler(int num, unsigned short segm_sel, unsigned short
	flags, intr_handler hndlr)
{
	unsigned int hndlr_addr = (unsigned int) hndlr;
	g_idt[num].base_lo = (unsigned short) (hndlr_addr & 0xFFFF);
	g_idt[num].segm_sel = segm_sel;
	g_idt[num].always0 = 0;
	g_idt[num].flags = flags;
	g_idt[num].base_hi = (unsigned short) (hndlr_addr >> 16);
}
// Функция инициализации системы прерываний: заполнение массива с адресами обработчиков
void intr_init()
{
	int i;
	int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);
	for(i = 0; i < idt_count; i++)
		intr_reg_handler(i, GDT_CS, 0x80 | IDT_TYPE_INTR,
		default_intr_handler); // segm_sel=0x8, P=1, DPL=0, Type=Intr
}

void intr_start()
{
	int idt_count = sizeof(g_idt) / sizeof(g_idt[0]);
	g_idtp.base = (unsigned int) (&g_idt[0]);
	g_idtp.limit = (sizeof (struct idt_entry) * idt_count) - 1;
	asm("lidt %0" : : "m" (g_idtp) );
}

void intr_enable()
{
	asm("sti");
}
void intr_disable()
{
	asm("cli");
}

static inline unsigned char inb (unsigned short port) // Чтение из порта
{
	unsigned char data;
	asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
	return data;
}
static inline void outb (unsigned short port, unsigned char data) // Запись
{
	asm volatile ("outb %b0, %w1" : : "a" (data), "Nd" (port));
}
static inline void outw (unsigned short port, unsigned short data) // Запись
{
	asm volatile ("outw %w0, %w1" : : "a" (data), "Nd" (port));
}

void on_key(unsigned char c);/* SELECTOR OF COMMANDS */

void keyb_process_keys()
{
	// Проверка что буфер PS/2 клавиатуры не пуст (младший бит присутствует)
	if (inb(0x64) & 0x01)
	{
	unsigned char scan_code;
	unsigned char state;
	scan_code = inb(0x60); // Считывание символа с PS/2 клавиатуры
	if (scan_code < 128) // Скан-коды выше 128 - это отпускание клавиши
		on_key(scan_code);
	}
}

void keyb_handler()
{
	asm("pusha");
	// Обработка поступивших данных
	keyb_process_keys();
	// Отправка контроллеру 8259 нотификации о том, что прерывание обработано
	outb(PIC1_PORT, 0x20);
	asm("popa; leave; iret");
}

void keyb_init()
{
	// Регистрация обработчика прерывания
	intr_reg_handler(0x09, GDT_CS, 0x80 | IDT_TYPE_INTR, keyb_handler);
	// segm_sel=0x8, P=1, DPL=0, Type=Intr
	// Разрешение только прерываний клавиатуры от контроллера 8259
	outb(PIC1_PORT + 1, 0xFF ^ 0x02); // 0xFF - все прерывания, 0x02 - бит IRQ1 	(клавиатура).
	// Разрешены будут только прерывания, чьи биты установлены в 0
}

// Функция переводит курсор на строку strnum (0 – самая верхняя) в позицию pos на этой строке (0 – самое левое положение).
void cursor_moveto(unsigned int strnum, unsigned int pos)
{
	unsigned short new_pos = (strnum * VIDEO_WIDTH) + pos;
	outb(CURSOR_PORT, 0x0F);
	outb(CURSOR_PORT + 1, (unsigned char)(new_pos & 0xFF));
	outb(CURSOR_PORT, 0x0E);
	outb(CURSOR_PORT + 1, (unsigned char)( (new_pos >> 8) & 0xFF));
}

void out_chr(int color, char ptr, unsigned int strnum, unsigned int cnt)
{
	unsigned char* video_buf = (unsigned char*) VIDEO_BUF_PTR;
	video_buf += VIDEO_WIDTH * 2 * strnum + cnt*2;
	video_buf[0] = (unsigned char) ptr; // Символ (код)
	video_buf[1] = color; // Цвет символа и фона
	video_buf += 2;
}
void out_str(int color, const char* ptr, unsigned int strnum)
{
	unsigned char* video_buf = (unsigned char*) VIDEO_BUF_PTR;
	video_buf += VIDEO_WIDTH * 2 * strnum;
	while (*ptr)
	{
	video_buf[0] = (unsigned char) *ptr; // Символ (код)
	video_buf[1] = color; // Цвет символа и фона
	video_buf += 2;
	ptr++;
	}
}
void out_str_num(int color, const char* ptr, unsigned int strnum, unsigned int cnt)
{
	unsigned char* video_buf = (unsigned char*) VIDEO_BUF_PTR;
	video_buf += VIDEO_WIDTH * 2 * strnum + cnt * 2;
	while (*ptr)
	{
	video_buf[0] = (unsigned char) *ptr; // Символ (код)
	video_buf[1] = color; // Цвет символа и фона
	video_buf += 2;
	ptr++;
	}
}

int strlen(const char *str)
{
	register const char *s;

	for (s = str; *s; ++s);
	return(s - str);
}
int strcmp(char *s, char *t)
{
	for(; *s == *t; s++, t++)
		if(*s == '\0') return 0;
	return *s - *t;
}
char *strcpy(char s[], const char t[])
{
	char *str = s;
	while ((*str++ = *t++) != 0)
		;
	return (s);
}
void reverse(char s[])
{
	int i, j;
	char c;

	for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}
void itoa(int n, char s[])
{
	int i, sign;

	if ((sign = n) < 0)  /* записываем знак */
		n = -n;          /* делаем n положительным числом */
	i = 0;
	do {       /* генерируем цифры в обратном порядке */
		s[i++] = n % 10 + '0';   /* берем следующую цифру */
	} while ((n /= 10) > 0);     /* удаляем */
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	reverse(s);
}

void clean_monitor()
{
	num_line = 0;
	int i;
	for(i = 0; i < 25; i++){
		out_str_num(0x07, "                    ", num_line, 0);
		out_str_num(0x07, "                    ", num_line, 20);
		out_str_num(0x07, "                      ", num_line++, 40);
	}
	num_line = 0;
}

//dictionary
#define N_WORDS 50
char *num_words;
struct dict_word
{
	char word[20];
	char tr_word[20];
}dictionary[N_WORDS];
char tran_word[20];

void en_to_fr()
{
	strcpy( dictionary[0].word, "apple");
	strcpy( dictionary[0].tr_word, "pomme");
	strcpy( dictionary[1].word, "hello");
	strcpy( dictionary[1].tr_word, "eh");
	strcpy( dictionary[2].word, "sorry");
	strcpy( dictionary[2].tr_word, "excuse");
	strcpy( dictionary[3].word, "manner");
	strcpy( dictionary[3].tr_word, "esrece");
	strcpy( dictionary[4].word, "woman");
	strcpy( dictionary[4].tr_word, "madame");
	strcpy( dictionary[5].word, "child");
	strcpy( dictionary[5].tr_word, "enfant");
	strcpy( dictionary[6].word, "boy");
	strcpy( dictionary[6].tr_word, "serviteur");
	strcpy( dictionary[7].word, "friend");
	strcpy( dictionary[7].tr_word, "amie");
	strcpy( dictionary[8].word, "parents");
	strcpy( dictionary[8].tr_word, "parents");
	strcpy( dictionary[9].word, "teacher");
	strcpy( dictionary[9].tr_word, "institutrice");
	strcpy( dictionary[10].word, "student");
	strcpy( dictionary[10].tr_word, "l'eleve");
	strcpy( dictionary[11].word, "cat");
	strcpy( dictionary[11].tr_word, "chat");
	strcpy( dictionary[12].word, "dog");
	strcpy( dictionary[12].tr_word, "clebs");
	strcpy( dictionary[13].word, "house");
	strcpy( dictionary[13].tr_word, "maison");
	strcpy( dictionary[14].word, "inn");
	strcpy( dictionary[14].tr_word, "auberge");
	strcpy( dictionary[15].word, "place");
	strcpy( dictionary[15].tr_word, "astro");
	strcpy( dictionary[16].word, "tree");
	strcpy( dictionary[16].tr_word, "arble");
	strcpy( dictionary[17].word, "forest");
	strcpy( dictionary[17].tr_word, "bois");
	strcpy( dictionary[18].word, "sea");
	strcpy( dictionary[18].tr_word, "marin");
	strcpy( dictionary[19].word, "room");
	strcpy( dictionary[19].tr_word, "salon");
	strcpy( dictionary[20].word, "window");
	strcpy( dictionary[20].tr_word, "hublot");
	strcpy( dictionary[21].word, "table");
	strcpy( dictionary[21].tr_word, "tableau");
	strcpy( dictionary[22].word, "bread");
	strcpy( dictionary[22].tr_word, "pain");
	strcpy( dictionary[23].word, "car");
	strcpy( dictionary[23].tr_word, "automobile");
	strcpy( dictionary[24].word, "morning ");
	strcpy( dictionary[24].tr_word, "matin");
	strcpy( dictionary[25].word, "evening");
	strcpy( dictionary[25].tr_word, "soir");
	strcpy( dictionary[26].word, "summer");
	strcpy( dictionary[26].tr_word, "estival");
	strcpy( dictionary[27].word, "name");
	strcpy( dictionary[27].tr_word, "qualifier");
	strcpy( dictionary[28].word, "face");
	strcpy( dictionary[28].tr_word, "visage");
	strcpy( dictionary[29].word, "bye");
	strcpy( dictionary[29].tr_word, "adieu");
	strcpy( dictionary[30].word, "zing");
	strcpy( dictionary[30].tr_word, "fronder");
	strcpy( dictionary[31].word, "young");
	strcpy( dictionary[31].tr_word, "jeune");
	strcpy( dictionary[32].word, "drink");
	strcpy( dictionary[32].tr_word, "boisson");			
	strcpy( dictionary[33].word, "wrong");
	strcpy( dictionary[33].tr_word, "incorrect");
	strcpy( dictionary[34].word, "work");
	strcpy( dictionary[34].tr_word, "ouvrer");			
	strcpy( dictionary[35].word, "void");
	strcpy( dictionary[35].tr_word, "vide");
	strcpy( dictionary[36].word, "upstart");
	strcpy( dictionary[36].tr_word, "enrichie");
	strcpy( dictionary[37].word, "turns");
	strcpy( dictionary[37].tr_word, "tourne");
	strcpy( dictionary[38].word, "omega");
	strcpy( dictionary[38].tr_word, "l'omega");
	strcpy( dictionary[39].word, "insertion");
	strcpy( dictionary[39].tr_word, "l'insertion");
	strcpy( dictionary[40].word, "eye");
	strcpy( dictionary[40].tr_word, "l'oieil");
	strcpy( dictionary[41].word, "clock");
	strcpy( dictionary[41].tr_word, "l'horloge");
	strcpy( dictionary[42].word, "chess");
	strcpy( dictionary[42].tr_word, "l'echec");
	strcpy( dictionary[43].word, "terror");
	strcpy( dictionary[43].tr_word, "terreur");
	strcpy( dictionary[44].word, "take");
	strcpy( dictionary[44].tr_word, "admettre");
	strcpy( dictionary[45].word, "sportsman");
	strcpy( dictionary[45].tr_word, "sportif");
	strcpy( dictionary[46].word, "root");
	strcpy( dictionary[46].tr_word, "enraciner");
	strcpy( dictionary[47].word, "queen");
	strcpy( dictionary[47].tr_word, "reine");
	strcpy( dictionary[48].word, "professional");
	strcpy( dictionary[48].tr_word, "pro");
	strcpy( dictionary[49].word, "embargo");
	strcpy( dictionary[49].tr_word, "embargo");
}

struct dict_word temp;
void sort()
{
	int p,m;
	for (p = 0; p < N_WORDS-1; p++) 
	{
		for (m = 0; m < N_WORDS - p-1; m++) 
		{
			if (strcmp(dictionary[m].word, dictionary[m + 1].word) > 0) 
			{
				strcpy(temp.word, dictionary[m].word);
				strcpy(temp.tr_word, dictionary[m].tr_word);
				strcpy(dictionary[m].word, dictionary[m + 1].word);
				strcpy(dictionary[m].tr_word, dictionary[m + 1].tr_word);
				strcpy(dictionary[m + 1].word, temp.word);
				strcpy(dictionary[m + 1].tr_word, temp.tr_word);
			}
		}
	}
}

int find_in_str_downloader()
{
	int i;
	for(i = 0; i < 26; i++)
		if (str_downloader[i] == key_buf[0])
			return 1;
	return 0;
}

void find()
{
	if(find_in_str_downloader() == 0)
	{
		out_str(0x07, sysmsg_emptystr, num_line++);
		out_str_num(0x07, "Error: word '", num_line - 1, 0);
		out_str_num(0x07, key_buf, num_line - 1, 13);
		out_str_num(0x07, "' is unknown", num_line - 1, 13 + strlen(key_buf));
		return;
	}

	int first = 0;
	int last = N_WORDS - 1;
	int mid;
	while (first < last)
	{
		mid = first + (last - first) / 2;
		if (strcmp(key_buf, dictionary[mid].word) < 0 || strcmp(key_buf, dictionary[mid].word) == 0)
			last = mid;
		else first = mid + 1;
	}

	if (strcmp(dictionary[last].word, key_buf) == 0)
	{
		out_str(0x07, dictionary[last].tr_word, num_line++);
	}
	else 
	{
		out_str(0x07, sysmsg_emptystr, num_line++);
		out_str_num(0x07, "Error: word '", num_line - 1, 0);
		out_str_num(0x07, key_buf, num_line - 1, 13);
		out_str_num(0x07, "' is unknown", num_line - 1, 13 + strlen(key_buf));
	}
}

char str_for_itoa[10]= { 0 };

int find_stat(char c)// сколько букв загружено по букве
{
	int i;
	int number = 0;
	int flag_stat = 0;

	for(i = 0; i < 26; i++)
		if (str_downloader[i] == c)
			flag_stat = 1;
		
	if (flag_stat == 1)
		for(i = 0; i < N_WORDS; i++)
			if (dictionary[i].word[0] == c)
				number++;

	return number;
}

void command_handler()
{
	int i;
	if (++num_line >= DEADLINE)
		clean_monitor();

	if (strcmp (key_com, strcpy(spec_copy, sysmsg_help)) == 0)
	{
		if (num_line >= DEADLINE - 6)
			clean_monitor();

		out_str(0x07, "info", num_line++);
		out_str(0x07, "dictinfo", num_line++);
		out_str(0x07, "translate [word]     For example: translate sorry", num_line++);
		out_str(0x07, "wordstat [letter]     For example: wordstat a", num_line++);
		out_str(0x07, "shutdown", num_line++);

		if (num_line >= DEADLINE)
			clean_monitor();
		return;
	}
 	else if (strcmp (key_com, strcpy(spec_copy, sysmsg_info)) == 0)
	{
		if (num_line >= DEADLINE - 4)
			clean_monitor();

		out_str(0x07, "DictOS: v.01. Developer: Eliseev Nikita, 23508/4, SPbPU, 2017", num_line++);
		out_str(0x07, "Compilers: bootloader: GNU, kernel: GCC", num_line++);
		out_str(0x07, "Our letters: ", num_line++);

		for(i = 0; i < 26; i++)
			if (str_downloader[i] != '_')
				out_chr(0x07, str_downloader[i], num_line - 1, 13 + i);

		if (num_line >= DEADLINE)
			clean_monitor();
		return;
	}	
	else if (strcmp (key_com, strcpy(spec_copy, sysmsg_dictinfo)) == 0)
	{
		if (num_line >= DEADLINE - 4)
			clean_monitor();

		out_str(0x07, "Dictionary: en -> fr", num_line++);
		out_str(0x07, "Number of words: ", num_line++);
		itoa(N_WORDS, str_for_itoa);
		out_str_num(0x07, str_for_itoa, num_line - 1, 17);

		out_str(0x07, "Number of loaded words: ", num_line++);
		int loaded_words = 0;
		for(i = 0; i < 26; i++)// всего слов загружено
			if (str_downloader[i] != '_')
				loaded_words += find_stat(str_downloader[i]);

		itoa(loaded_words, str_for_itoa);
		out_str_num(0x07, str_for_itoa, num_line - 1, 24);

		if (num_line >= DEADLINE)
			clean_monitor();
		return;
	}

	else if (strcmp (key_com, strcpy(spec_copy, sysmsg_translate)) == 0)
	{
		if (num_line >= DEADLINE - 2)
			clean_monitor();

		key_buf[--k_b]=0;
		find();

		if (num_line >= DEADLINE)
			clean_monitor();
		return;
	}

	else if (strcmp (key_com, strcpy(spec_copy, sysmsg_wordstat)) == 0)
	{
		if (num_line >= DEADLINE - 2)
			clean_monitor();

		int stat_num = find_stat(key_buf[0]);
		itoa(stat_num, str_for_itoa);

		out_str(0x07, sysmsg_emptystr, num_line++);
		out_str_num(0x07, "Letter '", num_line - 1, 0);
		out_chr(0x07, key_buf[0], num_line - 1, 8);
		out_str_num(0x07, "': ", num_line - 1, 9);
		out_str_num(0x07, str_for_itoa, num_line - 1, 12);
		if (stat_num != 1)
			out_str_num(0x07, "words loaded.", num_line - 1, 13 + strlen(str_for_itoa));
		else
			out_str_num(0x07, "word loaded.", num_line - 1, 13 + strlen(str_for_itoa));

		if (num_line >= DEADLINE)
			clean_monitor();
		return;
	}

	else if (strcmp (key_com, strcpy(spec_copy, sysmsg_shutdown)) == 0)
	{
		out_str(0x07, "Powering off...", num_line++);
		// This is the fix for Qemu support. 
		// Dmitry V. Reshetov, IKBS, SPbSTU, 2016
		outw (0xB004, 0x2000); // qemu < 1.7, ex. 1.6.2
		outw (0x604, 0x2000);  // qemu >= 1.7
	}

	else  out_str(0x07, sysmsg_unknown, num_line++);

		if (num_line >= DEADLINE)
			clean_monitor();
}

void clean()
{
	int n, n1;
	for (n = 0, n1=0; n < SIZE_COM, n1<SIZE_BUF; n++, n1++)
	{
		key_buf[n1] = 0;
		key_com[n] = 0;
	}
	k_c = k_b = 0;
}

char scan_ask(unsigned char a)
{
	char result;
	result = scancodes[a];
	return result;
}

char d;
void on_key(unsigned char c)/* SELECTOR OF COMMANDS */
{
	d = scan_ask(c);
	out_chr(0x07, d, num_line, stolb++);
	cursor_moveto(num_line, stolb);

	if (flag_buf == 0)
	        key_com[k_c++] = d;
	else key_buf[k_b++] = d;
     	
	if (c == ENTER){	
		if (num_line >= DEADLINE)
			clean_monitor();

		command_handler();

		out_str(0x07, sysmsg_entry, num_line);
		stolb = 2;
		cursor_moveto(num_line, stolb);

		clean();
		flag_buf = 0;
		return;
	}
	else if (c == BACKSPACE){		
		if (stolb < 4) return;

		unsigned char* video_buf = (unsigned char*) VIDEO_BUF_PTR;
		video_buf += 80 * 2 * num_line + (stolb - 2)*2 ;
		video_buf[1] = 0x00;//empty_color;
		video_buf[0] = 0x00;//empty_symbol;
		stolb -= 2;
		cursor_moveto(num_line, stolb);

		if (flag_buf == 0){
			if( k_c > 0){
			key_com[--k_c]=0;
			key_com[--k_c] = 0;
			}
		}
		else {
		if(k_b > 0){
		key_buf[--k_b]=0;
		key_buf[--k_b] = 0;
		}
		}
		return;
	}
	else if (c == SPACE){
		if (strcmp (key_com, strcpy(spec_copy, sysmsg_translate)) == 0)
		{
			flag_buf = 1;
		}
		else if (strcmp (key_com, strcpy(spec_copy, sysmsg_wordstat)) == 0)
		{
			flag_buf = 1;
		}
		return;
	}	
	else if (stolb == SIZE_COM + 2){
		out_str(0x07, sysmsg_unknown, ++num_line);
		out_str(0x07, sysmsg_entry, ++num_line);
		stolb = 2;
		cursor_moveto(num_line, 2);
		return;
	}
//	out_chr(0x07, d, num_line, stolb++);
}

void downloader()
{
	char buf;
	int i;
	unsigned char *video_buf = (unsigned char*) VIDEO_BUF_PTR;
	video_buf += VIDEO_WIDTH * 2;
	for(i = 0; i < 26; i++)
	{
		buf = *(video_buf + i * 2);
		if (buf != '_')
			str_downloader[i] = buf;
	}
}

extern "C" int kmain()
{
	downloader();	// Параметры загрузчика
	clean_monitor();

	en_to_fr();
	sort();	//

	const char* hello = "Welcome to DictOS (gcc edition)!";

	out_str(0x1F, hello, num_line++);
	out_str(0x07, sysmsg_entry, num_line);

	cursor_moveto(num_line, 2);
	
	intr_disable();
	intr_init();
	keyb_init();
	intr_start();
	intr_enable();

	while(1)
	{
	asm("hlt");
	}
	return 0;
}
