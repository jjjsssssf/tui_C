#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <windows.h>
#include <conio.h>
#include <wchar.h> 

#ifndef _getwch
    wint_t _getwch(void);
#endif

#define COLOR_STR_SIZE 20 
#define C_RESET "\033[0m"
#define C_BOLD  "\033[1m"

// Códigos de tecla
#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_ESC 27
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77

typedef struct {
    char* buffer;       // Buffer principal de saída
    size_t capacity;    // Capacidade total alocada
    size_t size;        // Bytes usados atualmente
    HANDLE hStdout;     // Handle do console do Windows
} Renderer;

// Inicializa o renderizador e configura UTF-8
Renderer* renderer_create() {
    Renderer* r = (Renderer*)malloc(sizeof(Renderer));
    if (!r) return NULL;

    // Define codificação do console para suportar caracteres especiais
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    r->capacity = 65536; // Começa com 64KB
    r->buffer = (char*)malloc(r->capacity);
    r->size = 0;
    r->hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!r->buffer) {
        fprintf(stderr, "Erro: Falha na alocacao do buffer.\n");
        free(r);
        return NULL;
    }

    return r;
}

// Libera toda a memória alocada
void renderer_destroy(Renderer* r) {
    if (r) {
        if (r->buffer) {
            free(r->buffer);
        }
        free(r);
    }
}

// Adiciona dados brutos ao buffer, redimensionando se necessário
void renderer_add_raw(Renderer* r, const char* dados, size_t tamanho) {
    // Expande capacidade (dobra) se o espaço for insuficiente
    if (r->size + tamanho >= r->capacity) {
        while (r->size + tamanho >= r->capacity) {
            r->capacity *= 2;
        }
        
        char* temp = (char*)realloc(r->buffer, r->capacity);
        if (!temp) {
            fprintf(stderr, "Erro fatal: Falha ao expandir buffer.\n");
            exit(1);
        }
        r->buffer = temp;
    }

    // Copia dados para a próxima posição livre
    memcpy(r->buffer + r->size, dados, tamanho);
    r->size += tamanho;
}

// Wrapper para adicionar strings terminadas em nulo
void renderer_add(Renderer* r, const char* content) {
    if (content == NULL) return;
    renderer_add_raw(r, content, strlen(content));
}

// Posiciona o cursor usando sequências ANSI
void renderer_move_cursor(Renderer* r, int y, int x) {
    char buffer[32];
    int len = sprintf(buffer, "\033[%d;%dH", y, x);
    renderer_add_raw(r, buffer, len);
}

// Despeja o conteúdo do buffer no console e reseta o índice
void renderer_render(Renderer* r) {
    if (r->size == 0) return;
    DWORD escritos = 0;
    WriteConsoleA(r->hStdout, r->buffer, (DWORD)r->size, &escritos, NULL);    
    r->size = 0;
}

typedef struct {
    const char* B_RESET;
    const char* B_SPACE;
} Interface;

// Inicializa a estrutura de interface e constantes
Interface* interface_create() {
    Interface* i = (Interface*)malloc(sizeof(Interface));
    i->B_RESET = "\033[0m";
    i->B_SPACE = " ";
    return i;
}

// Libera a memória da interface
void interface_destroy(Interface* i) {
    if (i) free(i);
}

// Calcula o comprimento visual da string (ignora ANSI e formatação UTF-8)
int interface_visible_len(const char* s) {
    if (!s) return 0;
    int length = 0;
    bool in_escape = false;
    size_t n = strlen(s);

    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c == 27) { // Início de sequência ANSI
            in_escape = true;
        } else if (in_escape) {
            // Termina sequência ANSI em 'm' ou letras (geralmente)
            if (c == 'm' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                in_escape = false;
            }
        } else {
            // Conta apenas bytes iniciais de caracteres UTF-8
            if ((c & 0xC0) != 0x80) {
                length++;
            }
        }
    }
    return length;
}

// Move o cursor utilizando o buffer do Renderer
void interface_move_cursor(Renderer* r, int y, int x) {
    char buffer[32];
    int len = sprintf(buffer, "\033[%d;%dH", y, x);
    renderer_add_raw(r, buffer, len);
}

// Quebra o texto em linhas baseado na largura (Word Wrap)
char** simple_word_wrap(const char* text, int width, int* num_lines) {
    int capacity = 16;
    char** lines = (char**)malloc(capacity * sizeof(char*));
    *num_lines = 0;

    const char* ptr = text;
    size_t len = strlen(text);
    const char* end = text + len;

    while (ptr < end) {
        // Ignora espaços no início da linha
        while (ptr < end && *ptr == ' ') ptr++;
        if (ptr >= end) break;

        const char* line_start = ptr;
        const char* line_end = line_start;
        int current_width = 0;

        // Processa palavras até preencher a largura ou encontrar quebra
        while (ptr < end && *ptr != '\n') {
            const char* word_start = ptr;
            int word_len_visible = 0;
            const char* word_end = ptr;

            // Calcula tamanho visual da palavra atual
            while (word_end < end && *word_end != ' ' && *word_end != '\n') {
                if ((*word_end & 0xC0) != 0x80) word_len_visible++;
                word_end++;
            }
            
            int spaces = (line_end == line_start) ? 0 : 1;
            
            // Verifica se a palavra cabe na linha atual
            if (current_width + spaces + word_len_visible <= width) {
                current_width += spaces + word_len_visible;
                line_end = word_end;
                ptr = word_end;
                if (ptr < end && *ptr == ' ') ptr++; 
            } else {
                break; // Não cabe, força nova linha
            }
        }
        
        // Trata caso de palavra única maior que a largura total
        if (line_end == line_start && ptr < end) {
             while (ptr < end && *ptr != ' ' && *ptr != '\n' && current_width < width) {
                ptr++;
                current_width++;
             }
             line_end = ptr;
        }

        // Aloca e copia a linha finalizada para o array
        int line_bytes = (int)(line_end - line_start);
        char* line_str = (char*)malloc(line_bytes + 1);
        memcpy(line_str, line_start, line_bytes);
        line_str[line_bytes] = '\0';

        // Expande array de linhas se necessário
        if (*num_lines >= capacity) {
            capacity *= 2;
            lines = (char**)realloc(lines, capacity * sizeof(char*));
        }
        lines[*num_lines] = line_str;
        (*num_lines)++;

        if (ptr < end && *ptr == '\n') ptr++;
    }

    return lines;
}

// Libera a memória da matriz de strings criada pelo word wrap
void free_wrapped_lines(char** lines, int count) {
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

// Preenche uma área retangular com espaços e cor de fundo (limpeza visual)
void interface_clear(Interface* ui, Renderer* r, int x, int y, int height, int width, const char* bg_color) {
    if (!bg_color) bg_color = "\033[40m";
    
    // Cria linha de espaços
    char* spaces = (char*)malloc(width + 3);
    memset(spaces, ' ', width + 2);
    spaces[width + 2] = '\0';

    for (int i = 0; i < height + 2; i++) {
        interface_move_cursor(r, y + i, x);
        renderer_add(r, bg_color);
        renderer_add(r, spaces);
        renderer_add(r, ui->B_RESET);
    }
    free(spaces);
}

// Desenha uma caixa com bordas, título e texto estático centralizado
void interface_draw(Interface* ui, Renderer* r, int x, int y, int height, int width, const char* title, const char* text_line, const char* bg_color, const char* border_color, const char* text_color) {    
    // Configurações padrão de cor
    if (!bg_color) bg_color = "";
    if (!border_color) border_color = "\033[37m";
    if (!text_color) text_color = "\033[37m";

    // Caracteres de borda dupla (UTF-8)
    const char* TL = "\xE2\x95\x94";
    const char* H  = "\xE2\x95\x90";
    const char* TR = "\xE2\x95\x97";
    const char* V  = "\xE2\x95\x91";
    const char* BL = "\xE2\x95\x9A";
    const char* BR = "\xE2\x95\x9D";

    // --- 1. Topo da caixa ---
    interface_move_cursor(r, y, x);
    renderer_add(r, bg_color);
    renderer_add(r, border_color);
    renderer_add(r, TL);

    // Insere título se houver espaço
    if (title && strlen(title) > 0) {
        int t_len = interface_visible_len(title);
        if (t_len + 2 < width) {
            renderer_add(r, " ");
            renderer_add(r, title);
            renderer_add(r, " ");
            for (int i = 0; i < width - t_len - 2; i++) renderer_add(r, H);
        } else {
            renderer_add(r, title);
        }
    } else {
        for (int i = 0; i < width; i++) renderer_add(r, H);
    }
    renderer_add(r, TR);
    renderer_add(r, ui->B_RESET);

    // Prepara o texto interno
    int num_lines = 0;
    char** lines = NULL;
    if (text_line && strlen(text_line) > 0) {
        lines = simple_word_wrap(text_line, width, &num_lines); 
    }

    // --- 2. Corpo da caixa ---
    for (int i = 0; i < height; i++) {
        interface_move_cursor(r, y + i + 1, x);
        renderer_add(r, bg_color);
        renderer_add(r, border_color);
        renderer_add(r, V);
        renderer_add(r, text_color);

        int padding = width;
        if (i < num_lines) {
            int len = interface_visible_len(lines[i]);
            if (len > width) {
                renderer_add(r, lines[i]); 
                padding = 0;
            } else {
                renderer_add(r, lines[i]);
                padding = width - len;
            }
        }

        // Preenche o resto da linha com espaços
        while (padding > 0) {
            renderer_add(r, " ");
            padding--;
        }

        renderer_add(r, border_color);
        renderer_add(r, V);
        renderer_add(r, ui->B_RESET);
    }
    if (lines) free_wrapped_lines(lines, num_lines);

    // --- 3. Base da caixa ---
    interface_move_cursor(r, y + height + 1, x);
    renderer_add(r, bg_color);
    renderer_add(r, border_color);
    renderer_add(r, BL);
    for (int i = 0; i < width; i++) renderer_add(r, H);
    renderer_add(r, BR);
    renderer_add(r, ui->B_RESET);
}

// Auxiliar para detectar bytes de caractere UTF-8
int get_utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;        // ASCII (1 byte)
    if ((c & 0xE0) == 0xC0) return 2;     // 2 bytes
    if ((c & 0xF0) == 0xE0) return 3;     // 3 bytes
    if ((c & 0xF8) == 0xF0) return 4;     // 4 bytes
    return 1; // Fallback
}

// Caixa de diálogo estilo RPG: digitação letra por letra, paginação e skip
void interface_drawspeak(Interface* ui, Renderer* r, int x, int y, int height, int width, const char* title, const char* texto, const char* bg_color, const char* border_color, const char* text_color, float speed) {
    
    if (!bg_color) bg_color = "\033[40m";
    if (!text_color) text_color = "\033[37m";

    // Quebra todo o texto antes de começar
    int total_lines = 0;
    char** wrapped_lines = simple_word_wrap(texto, width, &total_lines);
    
    DWORD sleep_ms = (DWORD)(speed * 1000);
    bool pular_animacao = false;

    // Loop de paginação (pula de 'height' em 'height' linhas)
    for (int i = 0; i < total_lines; i += height) {
        pular_animacao = false;
        
        // Desenha a moldura vazia
        interface_draw(ui, r, x, y, height, width, title, "", bg_color, border_color, text_color);
        renderer_render(r);
        
        int lines_in_page = (total_lines - i) > height ? height : (total_lines - i);

        // Processa cada linha da página atual
        for (int l = 0; l < lines_in_page; l++) {
            char* line = wrapped_lines[i + l];
            int pos_y = y + 1 + l;
            int pos_x = x + 1;

            // Se usuário pulou, imprime a linha inteira de uma vez
            if (pular_animacao) {
                interface_move_cursor(r, pos_y, pos_x);
                renderer_add(r, bg_color);
                renderer_add(r, text_color);
                renderer_add(r, line);
                renderer_render(r);
                continue;
            }
            
            // Efeito de digitação caractere por caractere
            size_t len = strlen(line);
            for (size_t k = 0; k < len; ) {
                interface_move_cursor(r, pos_y, pos_x);
                renderer_add(r, bg_color);
                renderer_add(r, text_color);
                
                unsigned char c = (unsigned char)line[k];
                int char_bytes = get_utf8_char_len(c);
                
                renderer_add_raw(r, &line[k], char_bytes);
                renderer_render(r); // Renderiza imediatamente

                k += char_bytes;
                pos_x++; 

                Sleep(sleep_ms);

                // Detecta tecla para pular animação
                if (_kbhit()) {
                    _getch(); // Consome tecla
                    pular_animacao = true;
                    // Completa o restante da linha atual
                    if (k < len) {
                        renderer_add(r, &line[k]); 
                        renderer_render(r);
                    }
                    break; 
                }
            }
        }

        // Indicador de "Próxima Página" (seta para baixo)
        interface_move_cursor(r, y + height, x + width);
        renderer_add(r, bg_color);
        renderer_add(r, border_color);
        renderer_add(r, "\xE2\x96\xBC"); 
        renderer_add(r, ui->B_RESET);
        renderer_render(r);

        // Espera interação do usuário (Enter, Espaço ou Z)
        while (true) {
            Sleep(50);
            if (_kbhit()) {
                int key = _getch();
                if (key == 13 || key == 32 || key == 'z' || key == 'Z') break;
            }
        }
    }

    free_wrapped_lines(wrapped_lines, total_lines);
}

// Variante de interface_draw (estrutura quase idêntica)
void interface_drawline(Interface* ui, Renderer* r, int x, int y, int height, int width, const char* title, const char* text_line, const char* bg_color, const char* border_color, const char* text_color, const char* border_style) {
    if (!bg_color) bg_color = "";
    if (!border_color) border_color = "\033[37m";
    if (!text_color) text_color = "\033[37m";
    
    const char* TL = "\xE2\x95\x94"; const char* H  = "\xE2\x95\x90";
    const char* TR = "\xE2\x95\x97"; const char* V  = "\xE2\x95\x91";
    const char* BL = "\xE2\x95\x9A"; const char* BR = "\xE2\x95\x9D";
    
    int total_lines = 0;
    char** lines = NULL;
    if (text_line && *text_line) {
        lines = simple_word_wrap(text_line, width, &total_lines);
    }

    // Topo
    interface_move_cursor(r, y, x);
    renderer_add(r, bg_color);
    renderer_add(r, border_color);
    renderer_add(r, TL);

    if (title && *title) {
        int t_len = interface_visible_len(title);
        if (t_len + 2 < width) {
            renderer_add(r, " ");
            renderer_add(r, title);
            renderer_add(r, " ");
            for (int k = 0; k < width - t_len - 2; k++) renderer_add(r, H);
        } else {
            renderer_add(r, title);
        }
    } else {
        for (int k = 0; k < width; k++) renderer_add(r, H);
    }
    renderer_add(r, TR);
    renderer_add(r, ui->B_RESET);

    // Corpo
    for (int i = 0; i < height; i++) {
        interface_move_cursor(r, y + i + 1, x);
        renderer_add(r, bg_color);
        renderer_add(r, border_color);
        renderer_add(r, V);
        renderer_add(r, text_color);

        int padding = width;
        if (i < total_lines) {
            int len = interface_visible_len(lines[i]);
            if (len > width) {
                 renderer_add(r, lines[i]);
                 padding = 0;
            } else {
                renderer_add(r, lines[i]);
                padding = width - len;
            }
        }

        while (padding > 0) {
            renderer_add(r, " ");
            padding--;
        }
        renderer_add(r, border_color);
        renderer_add(r, V);
        renderer_add(r, ui->B_RESET);
    }
    
    // Base
    interface_move_cursor(r, y + height + 1, x);
    renderer_add(r, bg_color);
    renderer_add(r, border_color);
    renderer_add(r, BL);
    for (int k = 0; k < width; k++) renderer_add(r, H);
    renderer_add(r, BR);
    renderer_add(r, ui->B_RESET);
    
    if (lines) free_wrapped_lines(lines, total_lines);
}

// Escreve texto solto na tela com efeito de digitação (sem moldura)
void interface_text_speak(Interface* ui, Renderer* r, int x, int y, const char* texto, const char* bg_color, const char* text_color, float speed) {
    
    if (!bg_color) bg_color = "\033[40m";
    if (!text_color) text_color = "\033[37m";
    
    DWORD sleep_ms = (DWORD)(speed * 1000);
    bool pular = false;
    size_t n = strlen(texto);
    
    interface_move_cursor(r, y, x);
    renderer_add(r, bg_color);
    renderer_add(r, text_color);
    renderer_render(r);
    
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)texto[i];
        
        // Tratamento de quebra de linha manual
        if (c == '\n') {
            y++;
            interface_move_cursor(r, y, x);
            renderer_add(r, bg_color);
            renderer_add(r, text_color);
            renderer_render(r);
            i++;
            continue;
        }
        
        int char_len = get_utf8_char_len(c);
        renderer_add_raw(r, (char*)&texto[i], char_len);
        renderer_render(r); // Flush obrigatório para animação
        
        i += char_len;
        
        if (!pular) {
            if (_kbhit()) {
                _getch(); 
                pular = true;
            } else {
                Sleep(sleep_ms);
            }
        }
    }
    renderer_add(r, ui->B_RESET);
    renderer_render(r);
}

// Imprime texto estático respeitando quebras de linha manuais (\n)
void interface_text_(Interface* ui, Renderer* r, int x, int y, const char* texto, const char* text_color, const char* bg_color) {
    
    if (!bg_color) bg_color = "";
    if (!text_color) text_color = "";
    
    const char* start = texto;
    const char* end;
    int current_y = y;

    // Itera sobre as linhas separadas por \n
    while ((end = strchr(start, '\n')) != NULL) {
        int len = (int)(end - start);        
        interface_move_cursor(r, current_y, x);
        if (*bg_color) renderer_add(r, bg_color);
        if (*text_color) renderer_add(r, text_color);
        renderer_add_raw(r, start, len);
        renderer_add(r, ui->B_RESET);
        
        start = end + 1;
        current_y++;
    }
    
    // Imprime o último pedaço (ou a string inteira se não houver \n)
    if (*start) {
        interface_move_cursor(r, current_y, x);
        if (*bg_color) renderer_add(r, bg_color);
        if (*text_color) renderer_add(r, text_color);
        renderer_add(r, start);
        renderer_add(r, ui->B_RESET);
    }
}

typedef struct {
    int last_key;
} Inputs;

// Inicializa a estrutura de controle de entrada
Inputs* inputs_create() {
    Inputs* inp = (Inputs*)malloc(sizeof(Inputs));
    return inp;
}

// Libera a memória da estrutura
void inputs_destroy(Inputs* input) {
    if (input) free(input);
}

// Calcula o tamanho visual da string (ignora sequências ANSI)
int inputs_visible_len(const char* s) {
    int l = 0;
    bool in_esc = false;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\x1b') {
            in_esc = true;
        } else if (in_esc && s[i] == 'm') {
            in_esc = false;
        } else if (!in_esc) {
            // Conta apenas bytes iniciais de caracteres UTF-8
            if ((s[i] & 0xC0) != 0x80) l++;
        }
    }
    return l;
}

// Função auxiliar para identificar bytes de continuação do UTF-8
bool is_utf8_continuation(char c) {
    // Em UTF-8, bytes que começam com 10xxxxxx (0x80 a 0xBF) são continuações
    return (c & 0xC0) == 0x80;
}

char* inputs_prompt(Inputs* input, Renderer* r, int x, int y, int max_len, const char* input_color) {
    if (max_len <= 0) max_len = 255;    
    
    renderer_move_cursor(r, y, x);
    if (input_color && *input_color) renderer_add(r, input_color);
    renderer_render(r);

    // Aloca buffer com folga (4x) pois caracteres UTF-8 podem ter até 4 bytes cada
    char* buffer = (char*)calloc((max_len * 4) + 1, sizeof(char)); 
    int current_bytes = 0; // Bytes totais usados no buffer
    int visual_len = 0;    // Quantidade de caracteres visíveis na tela
    
    wint_t ch; // Usamos wint_t para capturar o caractere Unicode completo

    while (true) {
        ch = _getwch(); // _getwch captura Unicode (Wide Char)
        
        // Ignora teclas estendidas (setas, F1-F12) se necessário
        if (ch == 0 || ch == 0xE0) {
            _getwch(); // Consome o segundo código da tecla estendida
            continue;
        }

        if (ch == KEY_ENTER) {
            // Limpa visualmente (preenche com espaços)
            renderer_move_cursor(r, y, x);
            for(int k=0; k < visual_len; k++) renderer_add(r, " ");
            
            // Reseta cor e cursor
            renderer_move_cursor(r, y, x);
            renderer_add(r, "\033[0m");
            renderer_render(r);
            
            return buffer;
        }
        else if (ch == KEY_BACKSPACE) {
            if (current_bytes > 0) {
                // Lógica de Backspace para UTF-8:
                // Remove bytes do buffer até encontrar o byte inicial do caractere
                do {
                    current_bytes--;
                } while (current_bytes > 0 && is_utf8_continuation(buffer[current_bytes]));
                
                buffer[current_bytes] = '\0'; // Trunca a string
                visual_len--;

                // Apaga visualmente (apenas 1 espaço visual para trás, o terminal cuida do resto)
                renderer_add(r, "\b \b"); 
                renderer_render(r);
            }
        }
        // Aceita qualquer caractere imprimível (acima de espaço)
        else if (ch >= 32) { 
            if (visual_len < max_len) {
                // Converte o Wide Char (ch) para bytes UTF-8
                char temp_utf8[5] = {0};
                
                // Função da API do Windows para converter WCHAR para UTF-8
                int bytes_written = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)&ch, 1, temp_utf8, 4, NULL, NULL);
                
                if (bytes_written > 0) {
                    // Copia os bytes gerados para o buffer principal
                    memcpy(buffer + current_bytes, temp_utf8, bytes_written);
                    current_bytes += bytes_written;
                    buffer[current_bytes] = '\0'; // Garante o fim da string
                    
                    visual_len++;

                    // Renderiza o caractere convertido
                    renderer_add(r, temp_utf8);
                    renderer_render(r);
                }
            }
        }
    }
}

// Menu de seleção vertical navegável com setas
int inputs_menu_selector_vertical(Inputs* input, Renderer* r, int x, int y, const char** options, int count, const char* bg_normal, const char* fg_normal,const char* bg_select, const char* fg_select,const char* bg_correct, const char* fg_correct) {
    int current_selection = 0;
    bool redraw = true;

    // Define cores padrão se não fornecidas
    if (!bg_normal) bg_normal = "\033[40m";
    if (!fg_normal) fg_normal = "\033[90m";
    if (!bg_select) bg_select = "\033[44m";
    if (!fg_select) fg_select = "\033[37m";
    if (!bg_correct) bg_correct = "\033[42m";
    if (!fg_correct) fg_correct = "\033[30m";

    // Calcula a largura máxima para alinhamento uniforme
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int l = inputs_visible_len(options[i]);
        if (l > max_len) max_len = l;
    }
    max_len += 4; // Margem extra

    renderer_add(r, "\033[?25l"); // Oculta cursor

    while (true) {
        // Redesenha apenas quando necessário
        if (redraw) {
            for (int i = 0; i < count; i++) {
                renderer_move_cursor(r, y + i, x);
                
                // Aplica estilo (selecionado vs normal)
                if (i == current_selection) {
                    renderer_add(r, bg_select);
                    renderer_add(r, fg_select);
                    renderer_add(r, "\033[1m"); 
                    renderer_add(r, "> ");
                } else {
                    renderer_add(r, bg_normal);
                    renderer_add(r, fg_normal);
                    renderer_add(r, "  ");
                }

                renderer_add(r, options[i]);

                // Preenche o restante da linha com espaços (padding)
                int len = inputs_visible_len(options[i]);
                int padding = max_len - (len + 2);
                for(int p=0; p<padding; p++) renderer_add(r, " ");

                renderer_add(r, "\033[0m");
            }
            renderer_render(r);
            redraw = false;
        }

        // Captura entrada
        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            ch = _getch(); // Teclas estendidas
            if (ch == KEY_UP) {
                current_selection--;
                if (current_selection < 0) current_selection = count - 1; // Wrap around
                redraw = true;
            } else if (ch == KEY_DOWN) {
                current_selection++;
                if (current_selection >= count) current_selection = 0; // Wrap around
                redraw = true;
            }
        } 
        else if (ch == KEY_ENTER) {
            // Efeito visual de confirmação
            renderer_move_cursor(r, y + current_selection, x);
            renderer_add(r, bg_correct);
            renderer_add(r, fg_correct);
            renderer_add(r, "\033[1m");
            renderer_add(r, "> ");
            renderer_add(r, options[current_selection]);
            
            int len = inputs_visible_len(options[current_selection]);
            int padding = max_len - (len + 2);
            for(int p=0; p<padding; p++) renderer_add(r, " ");
            
            renderer_add(r, "\033[0m");
            renderer_render(r);
            
            Sleep(150);
            renderer_add(r, "\033[?25h"); // Restaura cursor
            return current_selection;
        }
        else if (ch == KEY_ESC) {
            renderer_add(r, "\033[?25h");
            return -1;
        }
    }
}

// Menu de seleção horizontal (mesma lógica, layout diferente)
int inputs_menu_selector_horizontal(Inputs* input, Renderer* r, int x, int y, const char** options, int count, const char* bg_normal, const char* fg_normal,const char* bg_select, const char* fg_select,const char* bg_correct, const char* fg_correct) {
    int current_selection = 0;
    bool redraw = true;
    
    // Configuração de cores padrão
    if (!bg_normal) bg_normal = "\033[40m";
    if (!fg_normal) fg_normal = "\033[90m";
    if (!bg_select) bg_select = "\033[44m";
    if (!fg_select) fg_select = "\033[37m";
    if (!bg_correct) bg_correct = "\033[42m";
    if (!fg_correct) fg_correct = "\033[30m";

    renderer_add(r, "\033[?25l"); // Oculta cursor

    while (true) {
        if (redraw) {
            renderer_move_cursor(r, y, x);
            
            for (int i = 0; i < count; i++) {
                if (i == current_selection) {
                    renderer_add(r, bg_select);
                    renderer_add(r, fg_select);
                    renderer_add(r, "\033[1m");
                    renderer_add(r, "> ");
                } else {
                    renderer_add(r, bg_normal);
                    renderer_add(r, fg_normal);
                    renderer_add(r, "  ");
                }

                renderer_add(r, options[i]);
                renderer_add(r, "  "); // Espaçamento fixo à direita
                renderer_add(r, "\033[0m");
            }
            renderer_render(r);
            redraw = false;
        }

        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            ch = _getch();
            if (ch == KEY_LEFT) {
                current_selection--;
                if (current_selection < 0) current_selection = count - 1;
                redraw = true;
            } else if (ch == KEY_RIGHT) {
                current_selection++;
                if (current_selection >= count) current_selection = 0;
                redraw = true;
            }
        }
        else if (ch == KEY_ENTER) {
            // Calcula posição X do item selecionado para desenhar o feedback
            int temp_x = x;
            for(int i=0; i<current_selection; i++) {
                temp_x += 4 + inputs_visible_len(options[i]);
            }
            
            renderer_move_cursor(r, y, temp_x);
            renderer_add(r, bg_correct);
            renderer_add(r, fg_correct);
            renderer_add(r, "\033[1m");
            renderer_add(r, "> ");
            renderer_add(r, options[current_selection]);
            renderer_add(r, "  ");
            renderer_add(r, "\033[0m");
            renderer_render(r);
            
            Sleep(150);
            renderer_add(r, "\033[?25h");
            return current_selection;
        }
        else if (ch == KEY_ESC) {
            renderer_add(r, "\033[?25h");
            return -1;
        }
    }
}

// Captura tecla sem bloquear (non-blocking) ou retorna 0
int inputs_get_key() {
    if (_kbhit()) {
        int ch = _getch();
        // Normaliza teclas estendidas para range 1000+
        if (ch == 0 || ch == 0xE0) {
            return 1000 + _getch(); 
        }
        return ch;
    }
    return 0;
}

// Helper: Escala valor 0-255 para range reduzido de cores ANSI
static int _scale(int x) {
    if (x < 48) return 0;
    if (x < 115) return 1;
    return (x - 35) / 40;
}

// Helper: Converte RGB para índice ANSI 256 (0-255)
static int _rgb_to_ansi256(int r, int g, int b) {
    int r_ = _scale(r);
    int g_ = _scale(g);
    int b_ = _scale(b);
    return 16 + (36 * r_) + (6 * g_) + b_;
}

// Helper: Parseia string Hex (#RRGGBB) para inteiros
static void _hex_to_rgb(const char* hex, int* r, int* g, int* b) {
    if (!hex) { *r=0; *g=0; *b=0; return; }    
    if (*hex == '#') hex++;
    if (strlen(hex) < 6) { *r=0; *g=0; *b=0; return; }
    
    char r_str[3] = { hex[0], hex[1], '\0' };
    char g_str[3] = { hex[2], hex[3], '\0' };
    char b_str[3] = { hex[4], hex[5], '\0' };
    
    *r = (int)strtol(r_str, NULL, 16);
    *g = (int)strtol(g_str, NULL, 16);
    *b = (int)strtol(b_str, NULL, 16);
}

// Converte string Hex (#RRGGBB) para ID ANSI 256 cores (0-255)
int color_hex_to_ansi_id(const char* hex) {
    int r, g, b;
    _hex_to_rgb(hex, &r, &g, &b);
    return _rgb_to_ansi256(r, g, b);
}

// Gera sequência ANSI de cor de texto (Foreground) no buffer fornecido
void color_fg(char* buffer, const char* hex) {
    int ansi_code = color_hex_to_ansi_id(hex);
    sprintf(buffer, "\033[38;5;%dm", ansi_code);
}

// Gera sequência ANSI de cor de fundo (Background) no buffer fornecido
void color_bg(char* buffer, const char* hex) {
    int ansi_code = color_hex_to_ansi_id(hex);
    sprintf(buffer, "\033[48;5;%dm", ansi_code);
}

// Wrapper conveniente para color_fg usando buffer estático (retorno direto)
char* color_fg_s(const char* hex) {
    static char buffer[COLOR_STR_SIZE];
    color_fg(buffer, hex);
    return buffer;
}

// Wrapper conveniente para color_bg usando buffer estático
char* color_bg_s(const char* hex) {
    static char buffer[COLOR_STR_SIZE];
    color_bg(buffer, hex);
    return buffer;
}

void teste_1(Renderer* r, Interface* ui, Inputs* inp){
    // 1. Limpa Tela
    renderer_add(r, "\033[2J");
    const char* opcoes[] = {"Novo Jogo","Carregar Jogo","Opcoes","Creditos","Sair"};
    int quantidade = sizeof(opcoes) / sizeof(opcoes[0]);
    interface_draw(ui, r, 0, 1, 10, 20, "UI", "", "", "", "");
    int escolha = inputs_menu_selector_vertical(inp, r, 2, 2, opcoes, quantidade, "", "", "", "", "", "");
    if (escolha == -1) {
        interface_text_(ui, r, 2, 12, "O usuario apertou ESC (Cancelou).", "", "");
    } 
    else {
        switch (escolha) {
            case 0:
                interface_text_(ui, r, 2, 12, "Iniciando novo jogo...", "", "");
                break;
            case 1:
                interface_text_(ui, r, 2, 12, "Carregando save...", "", "");
                break;
            case 4:
                interface_text_(ui, r, 2, 12, "Saindo do sistema...", "", "");
                break;
        }
    }
}

void teste_2(Renderer* r, Interface* ui, Inputs* inp){
    renderer_add(r, "\033[2J");
    interface_text_(ui, r, 1, 1, "Qual será seu nome?", "", "");
    interface_text_(ui, r, 1, 2, ">", "", "");
    char* nome = inputs_prompt(inp, r, 2, 2, 8, "");
    interface_text_(ui, r, 2, 2, nome, "", "");
}

void teste_3(Renderer* r, Interface* ui, Inputs* inp){
    // melhor para loops
    char cor_fundo[COLOR_STR_SIZE];
    color_bg(cor_fundo, "#FFFFFF");
    char cor_texto[COLOR_STR_SIZE];
    color_fg(cor_texto, "#00FFFF");
    while (1){
        interface_draw(ui, r, 1, 1, 10, 20, "ui", "Olá Mundo Colorido",/*Melhor fora de loops*/cor_fundo, color_fg_s("#0000FF"), cor_texto);
        char* prompt = inputs_prompt(inp, r, 2, 5, 8, (color_fg_s("#00FFFF"),color_bg_s("#FFFFFF")));
        if (strcmp(prompt, "q") == 0){
            break;
        }
    }
}

int main(){
    Renderer* r = renderer_create();
    Inputs* inp = inputs_create();
    Interface* ui = interface_create();
    teste_3(r, ui, inp);
    renderer_render(r);
    inputs_destroy(inp);
    interface_destroy(ui);
    renderer_destroy(r);
    return 0;
}