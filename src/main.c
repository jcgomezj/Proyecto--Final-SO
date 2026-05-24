/*
 * main.c — IO-Editor con Pipeline: Compresión + Cifrado RC4
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  PIPELINE ARQUITECTÓNICO (Regla #6 del proyecto)           ║
 * ║                                                              ║
 * ║  GUARDAR:                                                    ║
 * ║    texto_plano → [LZ77/Huffman COMPRESS] → [RC4 ENCRYPT]   ║
 * ║              → disco (.lz77.rc4 / .huff.rc4)               ║
 * ║                                                              ║
 * ║  ABRIR:                                                      ║
 * ║    disco → [RC4 DECRYPT] → [LZ77/Huffman DECOMPRESS]       ║
 * ║         → texto_plano en RAM (gap buffer)                   ║
 * ║                                                              ║
 * ║  ¿Por qué PRIMERO comprimir y LUEGO cifrar?                 ║
 * ║  La encriptación aumenta la entropía a ~8 bits/byte         ║
 * ║  (distribución pseudoaleatoria). Los algoritmos de           ║
 * ║  compresión buscan patrones repetitivos. Si encriptamos      ║
 * ║  primero, no hay patrones → la compresión es inútil y       ║
 * ║  puede incluso AUMENTAR el tamaño del archivo.              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * GESTIÓN SEGURA DE LA LLAVE EN RAM:
 *   1. Se pide por consola (no hardcoded, no en argv).
 *   2. La página de memoria se bloquea con mlock() para evitar swap.
 *   3. Tras usar la llave, se borra con explicit_bzero().
 *   4. El contexto RC4 (S-box) también se destruye con explicit_bzero().
 */

#include "gap_buffer.h"
#include "file_io.h"
#include "huffman.h"
#include "lz77.h"
#include "rc4.h"

#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#define MAGIC_HUFF_RC4  "HUFR"
#define MAGIC_LZ77_RC4  "LZRC"
#define MAGIC_LEN       4
#define PASSPHRASE_MAX  128

static struct termios orig_termios;

static void disable_raw_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(unsigned long)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void build_filepath(const char *base, const char *ext,
                            char *out, size_t out_size)
{
    snprintf(out, out_size, "%s.%s", base, ext);
}

/* Lee passphrase sin eco en terminal */
static size_t read_passphrase(const char *prompt, char *out, size_t out_max)
{
    struct termios old_t, no_echo_t;
    tcgetattr(STDIN_FILENO, &old_t);
    no_echo_t = old_t;
    no_echo_t.c_lflag &= ~(unsigned long)(ECHO);
    no_echo_t.c_lflag |= ICANON;
    no_echo_t.c_iflag |= ICRNL;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &no_echo_t);

    write(STDOUT_FILENO, prompt, strlen(prompt));
    ssize_t n = read(STDIN_FILENO, out, out_max - 1);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_t);
    write(STDOUT_FILENO, "\r\n", 2);

    if (n <= 0) return 0;
    if (n > 0 && out[n-1] == '\n') n--;
    if (n > 0 && out[n-1] == '\r') n--;
    out[n] = '\0';
    return (size_t)n;
}

/*
 * encrypt_buffer: RC4 en RAM sobre el buffer dado.
 *
 * SEGURIDAD:
 *   - key_buf se borra con explicit_bzero() inmediatamente tras rc4_init().
 *   - El S-box RC4 se destruye con rc4_context_destroy() tras cifrar.
 *   - mlock() en key_buf debe hacerse ANTES de llamar esta función.
 */
static void encrypt_buffer(uint8_t *out, const uint8_t *in, size_t in_len,
                            char *key_buf, size_t key_len)
{
    RC4Context ctx;
    rc4_init(&ctx, (const uint8_t *)key_buf, key_len);
    /* ★ Llave borrada inmediatamente tras inicializar el S-box ★ */
    explicit_bzero(key_buf, PASSPHRASE_MAX);
    rc4_crypt(&ctx, in, out, in_len);
    /* ★ S-box destruido para no dejar basura criptográfica en stack ★ */
    rc4_context_destroy(&ctx);
}

static void render(const GapBuffer *gb, const char *filename)
{
    char   render_buf[PAGE_SIZE_BUF * 8];
    size_t rpos = 0;
    const char *clear = "\x1b[2J\x1b[H";
    memcpy(render_buf, clear, 7);
    rpos = 7;
    int hlen = snprintf(render_buf + rpos, sizeof(render_buf) - rpos,
        "\x1b[7m IO-Editor [RC4] | %s | Len: %zu | Ctrl+S: Guardar | Ctrl+X: Salir \x1b[0m\r\n",
        filename ? filename : "[nuevo]",
        gap_buffer_length(gb));
    if (hlen > 0) rpos += (size_t)hlen;
    size_t win   = 2000;
    size_t start = (gb->gap_start > win) ? gb->gap_start - win : 0;
    size_t before_len = gb->gap_start - start;
    if (before_len > 0 && rpos + before_len < sizeof(render_buf) - 10) {
        memcpy(render_buf + rpos, gb->buffer + start, before_len);
        rpos += before_len;
    }
    render_buf[rpos++] = '\x1b'; render_buf[rpos++] = '[';
    render_buf[rpos++] = '7';   render_buf[rpos++] = 'm';
    render_buf[rpos++] = ' ';
    render_buf[rpos++] = '\x1b'; render_buf[rpos++] = '[';
    render_buf[rpos++] = '0';   render_buf[rpos++] = 'm';
    size_t after_avail = gb->buf_size - gb->gap_end;
    size_t after_len   = (after_avail > win) ? win : after_avail;
    if (after_len > 0 && rpos + after_len < sizeof(render_buf) - 2) {
        memcpy(render_buf + rpos, gb->buffer + gb->gap_end, after_len);
        rpos += after_len;
    }
    (void)write(STDOUT_FILENO, render_buf, rpos);
}

int main(int argc, char *argv[])
{
    const char *filepath = (argc > 1) ? argv[1] : NULL;

    GapBuffer *gb = gap_buffer_create(INITIAL_GAP_SIZE);
    if (!gb) {
        write(STDERR_FILENO, "Error: memoria insuficiente\n", 28);
        return 1;
    }

    /*
     * Buffer de passphrase en stack + mlock().
     * mlock() evita que el SO envíe esta página al área de Swap,
     * mitigando el ataque de "Memory Scraping vía core dump / swap".
     */
    char passphrase[PASSPHRASE_MAX];
    memset(passphrase, 0, sizeof(passphrase));
    mlock(passphrase, PASSPHRASE_MAX);

    /* ── Cargar archivo detectando formato ── */
    if (filepath) {
        char  *data      = NULL;
        size_t data_size = 0;

        if (file_read_raw(filepath, &data, &data_size) == 0 && data_size >= MAGIC_LEN) {

            int is_huff_rc4 = (memcmp(data, MAGIC_HUFF_RC4, MAGIC_LEN) == 0);
            int is_lz77_rc4 = (memcmp(data, MAGIC_LZ77_RC4, MAGIC_LEN) == 0);

            if (is_huff_rc4 || is_lz77_rc4) {
                /* Descifrar primero, luego descomprimir */
                size_t plen = read_passphrase("Passphrase: ", passphrase, PASSPHRASE_MAX);

                if (plen > 0) {
                    size_t payload_len = data_size - MAGIC_LEN;
                    uint8_t *dec_payload = malloc(payload_len);
                    if (dec_payload) {
                        /* Pipeline de apertura: RC4 decrypt → decompress */
                        char pass_copy[PASSPHRASE_MAX];
                        mlock(pass_copy, PASSPHRASE_MAX);
                        memcpy(pass_copy, passphrase, plen + 1);
                        encrypt_buffer(dec_payload,
                                       (uint8_t *)data + MAGIC_LEN,
                                       payload_len, pass_copy, plen);
                        munlock(pass_copy, PASSPHRASE_MAX);

                        if (is_huff_rc4) {
                            HuffContext *hctx = huff_context_create();
                            if (hctx) {
                                uint8_t *plain = NULL; size_t plain_len = 0;
                                if (huff_decompress(hctx, dec_payload, payload_len,
                                                    &plain, &plain_len) == 0) {
                                    gap_buffer_load(gb, (char *)plain, plain_len);
                                    free(plain);
                                }
                                huff_context_free(hctx);
                            }
                        } else {
                            uint8_t *plain = NULL; size_t plain_len = 0;
                            if (lz77_decompress(dec_payload, payload_len,
                                                &plain, &plain_len) == 0) {
                                gap_buffer_load(gb, (char *)plain, plain_len);
                                free(plain);
                            }
                        }
                        explicit_bzero(dec_payload, payload_len);
                        free(dec_payload);
                    }
                }
                explicit_bzero(passphrase, PASSPHRASE_MAX);

            } else if (data_size >= 4 &&
                       data[0]=='H' && data[1]=='U' && data[2]=='F' && data[3]=='F') {
                HuffContext *hctx = huff_context_create();
                if (hctx) {
                    uint8_t *dec = NULL; size_t dec_len = 0;
                    if (huff_decompress(hctx, (uint8_t *)data, data_size, &dec, &dec_len) == 0) {
                        gap_buffer_load(gb, (char *)dec, dec_len);
                        free(dec);
                    }
                    huff_context_free(hctx);
                }
            } else if (data_size >= 4 &&
                       data[0]=='L' && data[1]=='Z' && data[2]=='7' && data[3]=='7') {
                uint8_t *dec = NULL; size_t dec_len = 0;
                if (lz77_decompress((uint8_t *)data, data_size, &dec, &dec_len) == 0) {
                    gap_buffer_load(gb, (char *)dec, dec_len);
                    free(dec);
                }
            } else {
                gap_buffer_load(gb, data, data_size);
            }
            free(data);
        }
    }

    enable_raw_mode();
    int running = 1, needs_redraw = 1;

    while (running) {
        if (needs_redraw) { render(gb, filepath); needs_redraw = 0; }

        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        needs_redraw = 1;

        switch (c) {

        case 24: /* Ctrl+X */
            running = 0;
            break;

        case 19: { /* Ctrl+S → pipeline Comprimir → Cifrar → Disco */
            if (!filepath) break;

            disable_raw_mode();
            size_t plen = read_passphrase("\r\nPassphrase para cifrar: ",
                                          passphrase, PASSPHRASE_MAX);
            enable_raw_mode();

            size_t text_len = gap_buffer_length(gb);
            char  *text = malloc(text_len + 1);
            if (!text) break;
            gap_buffer_extract(gb, text, text_len + 1);

            /* ════════════════════════════════════════════════════
             * PASO 1: COMPRESIÓN (antes del cifrado)
             *
             * Razón arquitectónica: el texto plano tiene redundancia
             * estadística (patrones, repeticiones). La compresión
             * Huffman/LZ77 explota esa redundancia reduciendo bytes.
             *
             * Si aplicáramos RC4 primero, generaríamos datos con
             * entropía ~8 bits/byte (pseudoaleatorios). La compresión
             * posterior no encontraría ningún patrón → tamaño igual
             * o mayor al original. Pipeline correcto: C→E, no E→C.
             * ════════════════════════════════════════════════════ */

            /* ── Huffman compress ── */
            uint8_t    *huff_out = NULL;
            size_t      huff_len = 0;
            HuffContext *hctx    = huff_context_create();
            if (hctx) {
                huff_compress(hctx, (uint8_t *)text, text_len, &huff_out, &huff_len);
                huff_context_free(hctx);
            }

            /* ── LZ77 compress ── */
            uint8_t  *lz77_out = NULL;
            size_t    lz77_len = 0;
            LZ77Stats lz77_stats;
            lz77_compress((uint8_t *)text, text_len, &lz77_out, &lz77_len, &lz77_stats);

            if (plen > 0) {
                /* ════════════════════════════════════════════════
                 * PASO 2: CIFRADO RC4 EN RAM (nunca toca disco en plano)
                 *
                 * RC4 es cifrado de flujo → sin padding.
                 * tamaño_cifrado == tamaño_comprimido exactamente.
                 * (AES-CBC añadiría hasta 16 bytes de padding; RC4 no.)
                 *
                 * Gestión de llave: encrypt_buffer() llama explicit_bzero()
                 * inmediatamente tras rc4_init(). La llave en texto plano
                 * existe en RAM por el mínimo tiempo posible.
                 * ════════════════════════════════════════════════ */

                /* Huffman + RC4 */
                if (huff_out) {
                    uint8_t *enc = malloc(huff_len);
                    if (enc) {
                        char pk[PASSPHRASE_MAX];
                        mlock(pk, PASSPHRASE_MAX);
                        memcpy(pk, passphrase, plen + 1);
                        encrypt_buffer(enc, huff_out, huff_len, pk, plen);
                        munlock(pk, PASSPHRASE_MAX);

                        /* PASO 3: escribir al disco */
                        char path[512];
                        build_filepath(filepath, "huff.rc4", path, sizeof(path));
                        size_t flen = MAGIC_LEN + huff_len;
                        uint8_t *fbuf = malloc(flen);
                        if (fbuf) {
                            memcpy(fbuf, MAGIC_HUFF_RC4, MAGIC_LEN);
                            memcpy(fbuf + MAGIC_LEN, enc, huff_len);
                            file_write_raw(path, (char *)fbuf, flen);
                            free(fbuf);
                        }
                        explicit_bzero(enc, huff_len);
                        free(enc);
                    }
                }

                /* LZ77 + RC4 */
                if (lz77_out) {
                    uint8_t *enc = malloc(lz77_len);
                    if (enc) {
                        char pk[PASSPHRASE_MAX];
                        mlock(pk, PASSPHRASE_MAX);
                        memcpy(pk, passphrase, plen + 1);
                        encrypt_buffer(enc, lz77_out, lz77_len, pk, plen);
                        munlock(pk, PASSPHRASE_MAX);

                        char path[512];
                        build_filepath(filepath, "lz77.rc4", path, sizeof(path));
                        size_t flen = MAGIC_LEN + lz77_len;
                        uint8_t *fbuf = malloc(flen);
                        if (fbuf) {
                            memcpy(fbuf, MAGIC_LZ77_RC4, MAGIC_LEN);
                            memcpy(fbuf + MAGIC_LEN, enc, lz77_len);
                            file_write_raw(path, (char *)fbuf, flen);
                            free(fbuf);
                        }
                        explicit_bzero(enc, lz77_len);
                        free(enc);
                    }
                }
            }

            /* Reporte de análisis */
            {
                char path_rep[512];
                build_filepath(filepath, "report", path_rep, sizeof(path_rep));
                char report[4096];
                int rlen = snprintf(report, sizeof(report),
                    "=================================================\n"
                    " REPORTE DE PIPELINE - IO-Editor (Proyecto 3 SO)\n"
                    "=================================================\n"
                    "Archivo fuente       : %s\n"
                    "Texto original       : %zu bytes\n"
                    "Cifrado              : RC4 (implementacion propia, sin padding)\n"
                    "\n"
                    "PIPELINE: texto → COMPRIMIR → CIFRAR RC4 → disco\n"
                    "\n"
                    "--- Huffman + RC4 ---\n"
                    "  Comprimido (Huffman): %zu bytes  (ratio: %.2f%%)\n"
                    "  Cifrado RC4         : %zu bytes  (= comprimido; RC4 es stream cipher)\n"
                    "  Archivo en disco    : %zu bytes  (+ %d bytes magic header)\n"
                    "\n"
                    "--- LZ77 + RC4 ---\n"
                    "  Comprimido (LZ77)   : %zu bytes  (ratio: %.2f%%)\n"
                    "  Cifrado RC4         : %zu bytes  (= comprimido)\n"
                    "  Archivo en disco    : %zu bytes  (+ %d bytes magic header)\n"
                    "  Tokens: %zu total | %zu literales | %zu matches\n"
                    "\n"
                    "--- Justificacion del orden Comprimir → Cifrar ---\n"
                    "  RC4 genera entropía ~8 bits/byte (pseudoaleatorio).\n"
                    "  Si se cifra primero, la compresión posterior NO reduce\n"
                    "  el tamaño: no hay patrones que explotar. El archivo\n"
                    "  resultante sería igual o más grande que el comprimido.\n"
                    "\n"
                    "--- Gestión segura de la llave en RAM ---\n"
                    "  Entrada      : consola sin eco (no hardcoded, no en argv)\n"
                    "  Anti-swap    : mlock() bloquea la página de RAM\n"
                    "  Destrucción  : explicit_bzero() inmediata tras rc4_init()\n"
                    "  S-box RC4    : rc4_context_destroy() tras cifrar\n"
                    "=================================================\n",
                    filepath,
                    text_len,
                    huff_len,
                    huff_len > 0 ? (1.0 - (double)huff_len/text_len)*100.0 : 0.0,
                    huff_len, huff_len + MAGIC_LEN, MAGIC_LEN,
                    lz77_len,
                    lz77_len > 0 ? (1.0 - (double)lz77_len/text_len)*100.0 : 0.0,
                    lz77_len, lz77_len + MAGIC_LEN, MAGIC_LEN,
                    lz77_stats.tokens_generated, lz77_stats.literal_tokens,
                    lz77_stats.match_tokens);
                if (rlen > 0)
                    file_write_raw(path_rep, report, (size_t)rlen);
            }

            explicit_bzero(passphrase, PASSPHRASE_MAX);
            free(huff_out);
            free(lz77_out);
            free(text);
            break;
        }

        case 127:
            gap_buffer_delete(gb);
            break;

        case '\x1b': {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) break;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) break;
            if (seq[0] == '[') {
                switch (seq[1]) {
                case 'A':
                    while (gb->gap_start > 0 && gb->buffer[gb->gap_start-1] != '\n')
                        gap_buffer_move_left(gb);
                    if (gb->gap_start > 0) gap_buffer_move_left(gb);
                    break;
                case 'B':
                    while (gb->gap_end < gb->buf_size && gb->buffer[gb->gap_end] != '\n')
                        gap_buffer_move_right(gb);
                    if (gb->gap_end < gb->buf_size) gap_buffer_move_right(gb);
                    break;
                case 'C': gap_buffer_move_right(gb); break;
                case 'D': gap_buffer_move_left(gb);  break;
                }
            }
            break;
        }

        case '\r':
            gap_buffer_insert(gb, '\n');
            break;

        default:
            if (c >= 32 || c == '\t') gap_buffer_insert(gb, c);
            break;
        }
    }

    explicit_bzero(passphrase, PASSPHRASE_MAX);
    munlock(passphrase, PASSPHRASE_MAX);
    (void)write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    gap_buffer_free(gb);
    return 0;
}
