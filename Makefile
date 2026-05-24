# Makefile - Editor con RC4 (P3)
# Compilación simple sin dependencias complejas

CC       = gcc
CFLAGS   = -Wall -Wextra -pedantic -std=c11 -I./include
LDFLAGS  = 
TARGET   = editor
SRCDIR   = src
OBJDIR   = obj
INCDIR   = include

# Archivos fuente
SRCS     = $(SRCDIR)/main.c $(SRCDIR)/rc4.c $(SRCDIR)/gap_buffer.c $(SRCDIR)/file_io.c $(SRCDIR)/huffman.c $(SRCDIR)/lz77.c
OBJS     = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS     = $(OBJS:.o=.d)

# Metas principales
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "🔗 Enlazando $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✅ Compilación exitosa: $@"

# Compilación de objetos
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "📝 Compilando $<..."
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Crear directorio obj si no existe
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Limpieza
clean:
	@echo "🧹 Limpiando archivos compilados..."
	@rm -rf $(OBJDIR) $(TARGET)
	@echo "✅ Limpieza completa"

# Limpieza profunda
distclean: clean
	@echo "🗑️  Limpieza total (incluyendo editor)"

# Ejecutar
run: $(TARGET)
	@echo "▶️  Ejecutando $(TARGET)..."
	./$(TARGET)

# Targets especiales
.PHONY: all clean distclean run

# Incluir dependencias
-include $(DEPS)
