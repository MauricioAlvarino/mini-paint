// ======================================================================
//  MINI PAINT - COMMIT 5: forma libre (poligono) con relleno opcional
//  Controles:
//    1 = Linea   2 = Triangulo   3 = Cuadrado   4 = Circulo   5 = Pincel
//    6 = Poligono libre
//    Clic izquierdo + arrastrar : dibuja la figura (vista previa mientras arrastras)
//    Soltar el clic             : la figura queda guardada en el lienzo
//    Poligono libre             : cada clic agrega un vertice
//                                 Enter o clic derecho = cerrar y guardar
//                                 Backspace = quitar ultimo vertice, ESC = cancelar
//    Clic en la paleta          : cambia el color (el blanco sirve de borrador)
//    Flecha arriba / abajo, rueda del mouse, + / - : cambia el tamano del pincel
//    F = relleno de figuras (triangulo, cuadrado, circulo, poligono) ON/OFF
//    G = grilla    C = limpiar    ESC = salir
// ======================================================================
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

// ---------------------------- SHADERS ---------------------------------
const char* vertexShaderSource = R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vColor;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core

in vec3 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

// ------------------------ CONFIGURACION DEL LIENZO ---------------------
// El area total es de 40 x 35 celdas: 30 filas para dibujar (arriba) y 5 filas
// para la paleta de colores (abajo). Cada celda mide 18 x 18 pixeles.
const int COLUMNAS = 40;
const int FILAS_LIENZO = 30;
const int FILAS_PALETA = 5;
const int FILAS = FILAS_LIENZO + FILAS_PALETA;
const int ANCHO_VENTANA = 720;
const int ALTO_VENTANA = 630;

// Rango permitido para el tamano del pincel (en celdas)
const int TAMANO_MIN = 1;
const int TAMANO_MAX = 8;

// ------------------------------ TIPOS ----------------------------------
// Una celda de la grilla: fila 0 = abajo, columna 0 = izquierda
struct Celda
{
    int fila;
    int col;
};

// Un color RGB con componentes entre 0 y 1
struct Color
{
    float r, g, b;
};

// Herramientas del programa
enum Herramienta
{
    HERRAMIENTA_LINEA,
    HERRAMIENTA_TRIANGULO,
    HERRAMIENTA_CUADRADO,
    HERRAMIENTA_CIRCULO,
    HERRAMIENTA_PINCEL,
    HERRAMIENTA_POLIGONO
};

// ------------------------------ PALETA ---------------------------------
const int NUM_COLORES = 10;
const Color PALETA[NUM_COLORES] = {
    { 0.00f, 0.00f, 0.00f },   // 0 negro
    { 0.40f, 0.40f, 0.40f },   // 1 gris
    { 0.90f, 0.15f, 0.15f },   // 2 rojo
    { 0.98f, 0.55f, 0.10f },   // 3 naranja
    { 0.98f, 0.85f, 0.15f },   // 4 amarillo
    { 0.20f, 0.70f, 0.30f },   // 5 verde
    { 0.35f, 0.75f, 0.95f },   // 6 celeste
    { 0.10f, 0.30f, 0.75f },   // 7 azul
    { 0.60f, 0.30f, 0.75f },   // 8 morado
    { 1.00f, 1.00f, 1.00f }    // 9 blanco (funciona como borrador)
};

const Color COLOR_GRILLA = { 0.75f, 0.75f, 0.75f };
const Color COLOR_FONDO_PALETA = { 0.70f, 0.70f, 0.70f };
const Color COLOR_MARCADOR = { 0.00f, 0.00f, 0.00f };

// --------------------------- ESTADO GLOBAL -----------------------------
Herramienta herramientaActual = HERRAMIENTA_LINEA;
int colorActual = 7;            // indice en PALETA (empieza en azul)
int tamanoPincel = 1;           // lado del pincel, en celdas
bool mostrarGrilla = true;      // true = se dibujan las lineas de la grilla
bool rellenoActivo = false;     // true = las figuras cerradas se rellenan
bool dibujando = false;         // true mientras el clic izquierdo esta presionado

int filaInicio = 0, colInicio = 0;   // celda donde se hizo clic (inicio de la figura)
int ultimaFila = 0, ultimaCol = 0;   // ultima celda visitada por el pincel

// Lienzo: una casilla por celda. 0 = vacia; k > 0 = pintada con PALETA[k - 1]
std::vector<unsigned char> lienzo(FILAS * COLUMNAS, 0);

// Vista previa: celdas de la figura que se esta dibujando ahora mismo
std::vector<Celda> vistaPrevia;

// Recorrido "crudo" del pincel (sin engrosar), celda por celda
std::vector<Celda> trazoPincel;

// Poligono libre: vertices ya puestos con clic y ultima celda donde estuvo el mouse
std::vector<Celda> verticesPoligono;
int cursorFila = FILAS_PALETA, cursorCol = 0;

// Identificadores de OpenGL (globales porque los callbacks los necesitan)
unsigned int VAOGrilla = 0, VBOGrilla = 0;
unsigned int VAOLienzo = 0, VBOLienzo = 0;
unsigned int VAOPrevia = 0, VBOPrevia = 0;
unsigned int VAOPaleta = 0, VBOPaleta = 0;

int verticesGrilla = 0;   // cantidad de vertices de cada buffer (para glDrawArrays)
int verticesLienzo = 0;
int verticesPrevia = 0;
int verticesPaleta = 0;

// ---------------------- UTILIDADES DE OPENGL ---------------------------

// Compila un shader y avisa por consola si hay errores de sintaxis en el GLSL
unsigned int compilarShader(unsigned int tipo, const char* codigo)
{
    unsigned int shader = glCreateShader(tipo);
    glShaderSource(shader, 1, &codigo, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Error al compilar shader:\n" << log << std::endl;
    }
    return shader;
}

// Crea el programa de shaders (vertex + fragment) y lo enlaza
unsigned int crearPrograma()
{
    unsigned int vs = compilarShader(GL_VERTEX_SHADER, vertexShaderSource);
    unsigned int fs = compilarShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    unsigned int programa = glCreateProgram();
    glAttachShader(programa, vs);
    glAttachShader(programa, fs);
    glLinkProgram(programa);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return programa;
}

// Crea un VAO + VBO vacios. Cada vertice tiene 6 floats: x, y, z, r, g, b
void crearBuffer(unsigned int& vao, unsigned int& vbo)
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Atributo 0: posicion (3 floats, empieza en el byte 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Atributo 1: color (3 floats, empieza despues de los 3 de la posicion)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

// Sube una lista de datos a un VBO y devuelve cuantos vertices son (6 floats cada uno)
int subirDatos(unsigned int vbo, const std::vector<float>& datos, unsigned int uso)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        datos.size() * sizeof(float),
        datos.empty() ? nullptr : datos.data(),
        uso
    );
    return (int)(datos.size() / 6);
}

// ------------------------- GEOMETRIA BASICA ----------------------------

// Agrega un vertice (posicion + color) a la lista
void agregarVertice(std::vector<float>& v, float x, float y, const Color& c)
{
    v.push_back(x); v.push_back(y); v.push_back(0.0f);
    v.push_back(c.r); v.push_back(c.g); v.push_back(c.b);
}

// Agrega un rectangulo relleno (2 triangulos) medido en celdas.
// (fila, col) es la esquina inferior izquierda.
void agregarRectangulo(std::vector<float>& v, int fila, int col, int alto, int ancho, const Color& c)
{
    float tamX = 2.0f / COLUMNAS;   // ancho de una celda en NDC
    float tamY = 2.0f / FILAS;      // alto de una celda en NDC

    float xIzq = -1.0f + col * tamX;
    float xDer = xIzq + ancho * tamX;
    float yAbajo = -1.0f + fila * tamY;
    float yArriba = yAbajo + alto * tamY;

    // Triangulo 1
    agregarVertice(v, xIzq, yArriba, c);
    agregarVertice(v, xIzq, yAbajo, c);
    agregarVertice(v, xDer, yAbajo, c);
    // Triangulo 2
    agregarVertice(v, xIzq, yArriba, c);
    agregarVertice(v, xDer, yAbajo, c);
    agregarVertice(v, xDer, yArriba, c);
}

// Genera las lineas de la grilla, SOLO sobre el area de dibujo (no sobre la paleta)
std::vector<float> crearVerticesGrilla()
{
    std::vector<float> v;
    float tamX = 2.0f / COLUMNAS;
    float tamY = 2.0f / FILAS;
    float yBase = -1.0f + FILAS_PALETA * tamY;   // borde inferior del lienzo

    for (int i = 0; i <= COLUMNAS; i++)           // lineas verticales
    {
        float x = -1.0f + i * tamX;
        agregarVertice(v, x, 1.0f, COLOR_GRILLA);
        agregarVertice(v, x, yBase, COLOR_GRILLA);
    }

    for (int j = FILAS_PALETA; j <= FILAS; j++)   // lineas horizontales
    {
        float y = -1.0f + j * tamY;
        agregarVertice(v, -1.0f, y, COLOR_GRILLA);
        agregarVertice(v, 1.0f, y, COLOR_GRILLA);
    }

    return v;
}

// Convierte una lista de celdas en triangulos, todas del mismo color
std::vector<float> celdasAVertices(const std::vector<Celda>& celdas, const Color& color)
{
    std::vector<float> v;
    for (const Celda& c : celdas)
        agregarRectangulo(v, c.fila, c.col, 1, 1, color);
    return v;
}

// ------------------------- LOGICA DEL LIENZO ---------------------------

// Una celda es valida si esta dentro del AREA DE DIBUJO (la paleta no cuenta)
bool celdaValida(int fila, int col)
{
    return fila >= FILAS_PALETA && fila < FILAS && col >= 0 && col < COLUMNAS;
}

// Convierte la posicion del mouse (pixeles) en una celda (fila, columna)
void pixelACelda(GLFWwindow* window, double xpos, double ypos, int& fila, int& col)
{
    int ancho, alto;
    glfwGetWindowSize(window, &ancho, &alto);

    // Pixeles -> NDC. En pantalla Y crece hacia abajo, en OpenGL hacia arriba (por eso se invierte)
    float ndcX = (float)(2.0 * xpos / ancho - 1.0);
    float ndcY = (float)(1.0 - 2.0 * ypos / alto);

    // NDC -> indice de celda
    col = (int)((ndcX + 1.0f) / (2.0f / COLUMNAS));
    fila = (int)((ndcY + 1.0f) / (2.0f / FILAS));

    // Si el mouse esta fuera de la ventana, se ajusta al borde
    if (col < 0) col = 0;
    if (col >= COLUMNAS) col = COLUMNAS - 1;
    if (fila < 0) fila = 0;
    if (fila >= FILAS) fila = FILAS - 1;
}

// Algoritmo de Bresenham para lineas, aplicado sobre celdas en vez de pixeles
std::vector<Celda> bresenhamLinea(int col0, int fila0, int col1, int fila1)
{
    std::vector<Celda> celdas;

    int dx = std::abs(col1 - col0);     // distancia horizontal en celdas
    int dy = std::abs(fila1 - fila0);   // distancia vertical en celdas
    int sx = (col0 < col1) ? 1 : -1;    // sentido del avance en columnas
    int sy = (fila0 < fila1) ? 1 : -1;  // sentido del avance en filas
    int err = dx - dy;                  // termino de error

    int col = col0;
    int fila = fila0;

    while (true)
    {
        celdas.push_back({ fila, col });

        if (col == col1 && fila == fila1)
            break;

        int e2 = 2 * err;

        if (e2 > -dy)   // avanzar en columna
        {
            err -= dy;
            col += sx;
        }
        if (e2 < dx)    // avanzar en fila
        {
            err += dx;
            fila += sy;
        }
    }

    return celdas;
}

// ---------------------- FIGURAS (usan Bresenham) -----------------------

// Agrega al final de "destino" todas las celdas de "origen"
void agregarCeldas(std::vector<Celda>& destino, const std::vector<Celda>& origen)
{
    destino.insert(destino.end(), origen.begin(), origen.end());
}

// Descarta las celdas que quedaron fuera del lienzo (una figura puede salirse del borde)
std::vector<Celda> filtrarValidas(const std::vector<Celda>& celdas)
{
    std::vector<Celda> validas;
    for (const Celda& c : celdas)
        if (celdaValida(c.fila, c.col))
            validas.push_back(c);
    return validas;
}

// Algoritmo del punto medio (Bresenham) para circunferencias.
// Solo calcula UN octante; los otros 7 salen por simetria.
std::vector<Celda> bresenhamCirculo(int filaC, int colC, int radio)
{
    std::vector<Celda> celdas;

    int x = 0;
    int y = radio;
    int p = 1 - radio;      // parametro de decision inicial: P0 = 1 - R

    // Dado un punto (x, y) del octante, agrega sus 8 simetrias alrededor del centro
    auto agregarOctantes = [&](int x, int y)
    {
        celdas.push_back({ filaC + y, colC + x });
        celdas.push_back({ filaC + x, colC + y });
        celdas.push_back({ filaC + x, colC - y });
        celdas.push_back({ filaC + y, colC - x });
        celdas.push_back({ filaC - y, colC - x });
        celdas.push_back({ filaC - x, colC - y });
        celdas.push_back({ filaC - x, colC + y });
        celdas.push_back({ filaC - y, colC + x });
    };

    agregarOctantes(x, y);

    while (x < y)           // se detiene al llegar a la diagonal de 45 grados
    {
        x = x + 1;          // X siempre avanza una celda

        if (p < 0)          // caso 1: Y no cambia
        {
            p = p + 2 * x + 1;
        }
        else                // caso 2: Y baja una celda
        {
            y = y - 1;
            p = p + 2 * x - 2 * y + 1;
        }

        agregarOctantes(x, y);
    }

    return celdas;
}

// TRIANGULO: la base queda en la fila donde se hizo clic y la punta en la fila del mouse.
std::vector<Celda> figuraTriangulo(int colIni, int filaIni, int colFin, int filaFin)
{
    std::vector<Celda> celdas;

    int izq = std::min(colIni, colFin);
    int der = std::max(colIni, colFin);
    int medio = (izq + der) / 2;

    agregarCeldas(celdas, bresenhamLinea(izq, filaIni, der, filaIni));      // base
    agregarCeldas(celdas, bresenhamLinea(der, filaIni, medio, filaFin));    // lado derecho
    agregarCeldas(celdas, bresenhamLinea(medio, filaFin, izq, filaIni));    // lado izquierdo

    return celdas;
}

// CUADRADO: el clic es una esquina y el lado es la mayor distancia (horizontal o vertical)
std::vector<Celda> figuraCuadrado(int colIni, int filaIni, int colFin, int filaFin)
{
    std::vector<Celda> celdas;

    int dx = colFin - colIni;
    int dy = filaFin - filaIni;
    int lado = std::max(std::abs(dx), std::abs(dy));
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;

    int col1 = colIni + sx * lado;      // esquina opuesta
    int fila1 = filaIni + sy * lado;

    agregarCeldas(celdas, bresenhamLinea(colIni, filaIni, col1, filaIni));
    agregarCeldas(celdas, bresenhamLinea(col1, filaIni, col1, fila1));
    agregarCeldas(celdas, bresenhamLinea(col1, fila1, colIni, fila1));
    agregarCeldas(celdas, bresenhamLinea(colIni, fila1, colIni, filaIni));

    return celdas;
}

// CIRCULO: el clic es el centro y el radio es la distancia (Pitagoras) hasta el mouse
std::vector<Celda> figuraCirculo(int colIni, int filaIni, int colFin, int filaFin)
{
    int dFila = filaFin - filaIni;
    int dCol = colFin - colIni;
    int radio = (int)std::round(std::sqrt((double)(dFila * dFila + dCol * dCol)));

    if (radio <= 0)
        return { { filaIni, colIni } };     // sin movimiento: solo el centro

    return bresenhamCirculo(filaIni, colIni, radio);
}

// RELLENO (scanline): para cada fila del contorno se busca la columna mas a la
// izquierda y la mas a la derecha, y se pintan todas las celdas entre ellas.
// Sirve para figuras convexas (triangulo, cuadrado, circulo).
// Se llama ANTES de filtrar, asi una figura que se sale del lienzo se rellena bien.
std::vector<Celda> rellenarFilas(const std::vector<Celda>& contorno)
{
    std::vector<Celda> relleno;
    if (contorno.empty())
        return relleno;

    int minF = contorno[0].fila;
    int maxF = contorno[0].fila;
    for (const Celda& c : contorno)
    {
        minF = std::min(minF, c.fila);
        maxF = std::max(maxF, c.fila);
    }

    int n = maxF - minF + 1;                       // cantidad de filas que ocupa la figura
    std::vector<int> minC(n, 1000000);             // columna minima de cada fila
    std::vector<int> maxC(n, -1000000);            // columna maxima de cada fila

    for (const Celda& c : contorno)
    {
        int i = c.fila - minF;
        minC[i] = std::min(minC[i], c.col);
        maxC[i] = std::max(maxC[i], c.col);
    }

    for (int i = 0; i < n; i++)
        for (int col = minC[i]; col <= maxC[i]; col++)
            relleno.push_back({ minF + i, col });

    return relleno;
}

// Elige la figura segun la herramienta activa (el pincel y el poligono se manejan aparte).
// Si "relleno" es true, el triangulo, el cuadrado y el circulo salen rellenos.
std::vector<Celda> calcularFigura(Herramienta herramienta, int colIni, int filaIni, int colFin, int filaFin, bool relleno)
{
    std::vector<Celda> celdas;

    switch (herramienta)
    {
    case HERRAMIENTA_LINEA:
        celdas = bresenhamLinea(colIni, filaIni, colFin, filaFin);
        break;
    case HERRAMIENTA_TRIANGULO:
        celdas = figuraTriangulo(colIni, filaIni, colFin, filaFin);
        break;
    case HERRAMIENTA_CUADRADO:
        celdas = figuraCuadrado(colIni, filaIni, colFin, filaFin);
        break;
    case HERRAMIENTA_CIRCULO:
        celdas = figuraCirculo(colIni, filaIni, colFin, filaFin);
        break;
    case HERRAMIENTA_PINCEL:
    case HERRAMIENTA_POLIGONO:
        break;
    }

    // La linea, el pincel y el poligono no usan este relleno
    if (relleno && herramienta != HERRAMIENTA_LINEA
                && herramienta != HERRAMIENTA_PINCEL
                && herramienta != HERRAMIENTA_POLIGONO)
        celdas = rellenarFilas(celdas);

    return filtrarValidas(celdas);
}

// ------------------------- FORMA LIBRE (POLIGONO) ----------------------

// Contorno del poligono: une cada vertice con el siguiente usando Bresenham.
// Si "cerrar" es true (y hay 3 o mas vertices), une tambien el ultimo con el primero.
std::vector<Celda> contornoPoligono(const std::vector<Celda>& v, bool cerrar)
{
    std::vector<Celda> celdas;

    if (v.empty())
        return celdas;

    if (v.size() == 1)
    {
        celdas.push_back(v[0]);
        return celdas;
    }

    for (size_t i = 0; i + 1 < v.size(); i++)
        agregarCeldas(celdas, bresenhamLinea(v[i].col, v[i].fila, v[i + 1].col, v[i + 1].fila));

    if (cerrar && v.size() >= 3)
        agregarCeldas(celdas, bresenhamLinea(v.back().col, v.back().fila, v[0].col, v[0].fila));

    return celdas;
}

// RELLENO DE POLIGONO (scanline con regla par-impar): funciona tambien con formas
// concavas. Para cada fila se buscan los cruces con los lados del poligono, se
// ordenan de izquierda a derecha y se pintan los tramos entre cruce 1-2, 3-4, etc.
std::vector<Celda> rellenarPoligono(const std::vector<Celda>& v)
{
    std::vector<Celda> relleno;
    if (v.size() < 3)
        return relleno;

    int minF = v[0].fila;
    int maxF = v[0].fila;
    for (const Celda& c : v)
    {
        minF = std::min(minF, c.fila);
        maxF = std::max(maxF, c.fila);
    }

    for (int fila = minF; fila <= maxF; fila++)
    {
        std::vector<double> cruces;

        for (size_t i = 0; i < v.size(); i++)
        {
            const Celda& a = v[i];
            const Celda& b = v[(i + 1) % v.size()];

            // Regla de intervalo semiabierto: el lado cruza la fila si un extremo
            // esta en "fila" o por debajo y el otro estrictamente por encima
            if ((a.fila <= fila && b.fila > fila) || (b.fila <= fila && a.fila > fila))
            {
                double x = a.col + (double)(fila - a.fila) * (b.col - a.col) / (double)(b.fila - a.fila);
                cruces.push_back(x);
            }
        }

        std::sort(cruces.begin(), cruces.end());

        for (size_t i = 0; i + 1 < cruces.size(); i += 2)
        {
            int col0 = (int)std::ceil(cruces[i]);
            int col1 = (int)std::floor(cruces[i + 1]);
            for (int col = col0; col <= col1; col++)
                relleno.push_back({ fila, col });
        }
    }

    return relleno;
}

// Celdas del poligono completo: contorno + (si hay relleno) el interior
std::vector<Celda> calcularPoligono(const std::vector<Celda>& v, bool cerrar, bool relleno)
{
    std::vector<Celda> celdas = contornoPoligono(v, cerrar);

    if (relleno && v.size() >= 3)
        agregarCeldas(celdas, rellenarPoligono(v));

    return filtrarValidas(celdas);
}

// TAMANO DEL PINCEL: cada celda de la figura se convierte en un cuadrado de
// "tamano x tamano" celdas. Se usa una marca para no repetir celdas.
std::vector<Celda> engrosar(const std::vector<Celda>& celdas, int tamano)
{
    std::vector<unsigned char> marca(FILAS * COLUMNAS, 0);
    std::vector<Celda> resultado;

    int atras = (tamano - 1) / 2;   // celdas hacia abajo / izquierda
    int adelante = tamano / 2;      // celdas hacia arriba / derecha

    for (const Celda& c : celdas)
    {
        for (int df = -atras; df <= adelante; df++)
        {
            for (int dc = -atras; dc <= adelante; dc++)
            {
                int f = c.fila + df;
                int k = c.col + dc;

                if (celdaValida(f, k) && marca[f * COLUMNAS + k] == 0)
                {
                    marca[f * COLUMNAS + k] = 1;
                    resultado.push_back({ f, k });
                }
            }
        }
    }

    return resultado;
}

// ----------------------- BUFFERS Y ESTADO DEL DIBUJO -------------------

// Reconstruye el buffer del lienzo: cada celda pintada usa su propio color
void actualizarBufferLienzo()
{
    std::vector<float> v;

    for (int fila = FILAS_PALETA; fila < FILAS; fila++)
    {
        for (int col = 0; col < COLUMNAS; col++)
        {
            unsigned char valor = lienzo[fila * COLUMNAS + col];
            if (valor > 0)
                agregarRectangulo(v, fila, col, 1, 1, PALETA[valor - 1]);
        }
    }

    verticesLienzo = subirDatos(VBOLienzo, v, GL_DYNAMIC_DRAW);
}

// Sube la vista previa actual (con el color elegido) a la GPU
void actualizarBufferPrevia()
{
    verticesPrevia = subirDatos(VBOPrevia, celdasAVertices(vistaPrevia, PALETA[colorActual]), GL_DYNAMIC_DRAW);
}

// Dibuja la barra de la paleta: fondo gris, una muestra por color y un marcador
// negro debajo de la muestra seleccionada
void actualizarBufferPaleta()
{
    std::vector<float> v;

    agregarRectangulo(v, 0, 0, FILAS_PALETA, COLUMNAS, COLOR_FONDO_PALETA);

    for (int i = 0; i < NUM_COLORES; i++)
    {
        int col0 = i * 4;   // cada muestra ocupa 3 columnas y deja 1 de separacion
        agregarRectangulo(v, 1, col0, 3, 3, PALETA[i]);

        if (i == colorActual)
            agregarRectangulo(v, 0, col0, 1, 3, COLOR_MARCADOR);
    }

    verticesPaleta = subirDatos(VBOPaleta, v, GL_DYNAMIC_DRAW);
}

// Pasa las celdas de la vista previa al lienzo con el color actual
void confirmarFigura()
{
    for (const Celda& c : vistaPrevia)
        lienzo[c.fila * COLUMNAS + c.col] = (unsigned char)(colorActual + 1);

    vistaPrevia.clear();
    trazoPincel.clear();
    actualizarBufferLienzo();
    actualizarBufferPrevia();
}

// Vista previa del poligono en curso: los vertices puestos + el mouse como
// vertice temporal. Con relleno activo se muestra ya cerrado y relleno.
void actualizarVistaPoligono()
{
    if (verticesPoligono.empty())
    {
        vistaPrevia.clear();
        actualizarBufferPrevia();
        return;
    }

    std::vector<Celda> temporal = verticesPoligono;
    temporal.push_back({ cursorFila, cursorCol });

    vistaPrevia = engrosar(calcularPoligono(temporal, rellenoActivo, rellenoActivo), tamanoPincel);
    actualizarBufferPrevia();
}

// Cierra el poligono (une el ultimo vertice con el primero), lo rellena si
// corresponde y lo guarda en el lienzo
void cerrarPoligono()
{
    if (verticesPoligono.empty())
        return;

    vistaPrevia = engrosar(calcularPoligono(verticesPoligono, true, rellenoActivo), tamanoPincel);
    confirmarFigura();
    verticesPoligono.clear();
}

// Descarta el poligono en curso sin guardarlo
void cancelarPoligono()
{
    verticesPoligono.clear();
    vistaPrevia.clear();
    actualizarBufferPrevia();
}

// Borra todo el lienzo
void limpiarLienzo()
{
    std::fill(lienzo.begin(), lienzo.end(), 0);
    vistaPrevia.clear();
    trazoPincel.clear();
    verticesPoligono.clear();
    actualizarBufferLienzo();
    actualizarBufferPrevia();
}

// Nombre de la herramienta activa (para la barra de titulo)
const char* nombreHerramienta(Herramienta h)
{
    switch (h)
    {
    case HERRAMIENTA_LINEA:     return "Linea";
    case HERRAMIENTA_TRIANGULO: return "Triangulo";
    case HERRAMIENTA_CUADRADO:  return "Cuadrado";
    case HERRAMIENTA_CIRCULO:   return "Circulo";
    case HERRAMIENTA_PINCEL:    return "Pincel";
    case HERRAMIENTA_POLIGONO:  return "Poligono libre";
    }
    return "?";
}

// Muestra en la barra de titulo la herramienta, el tamano, el relleno y la grilla
void actualizarTitulo(GLFWwindow* window)
{
    std::string titulo = "Mini Paint | Herramienta: ";
    titulo += nombreHerramienta(herramientaActual);
    titulo += " | Tamano: " + std::to_string(tamanoPincel);
    titulo += " | Relleno: ";
    titulo += rellenoActivo ? "ON" : "OFF";
    titulo += " | Grilla: ";
    titulo += mostrarGrilla ? "ON" : "OFF";
    titulo += "  (1-6 herramienta, F = relleno, Enter/clic der = cerrar poligono, G = grilla, C = limpiar)";
    glfwSetWindowTitle(window, titulo.c_str());
}

// Cambia de herramienta (si habia un poligono a medias, se cancela)
void seleccionarHerramienta(GLFWwindow* window, Herramienta nueva)
{
    if (nueva != herramientaActual)
        cancelarPoligono();

    herramientaActual = nueva;
    actualizarTitulo(window);
}

// Cambia el tamano del pincel sin salirse del rango permitido
void cambiarTamano(GLFWwindow* window, int cambio)
{
    tamanoPincel = std::max(TAMANO_MIN, std::min(TAMANO_MAX, tamanoPincel + cambio));
    actualizarTitulo(window);
}

// Si el clic cayo sobre una muestra de la paleta, la selecciona
void seleccionarColorEnPaleta(GLFWwindow* window, int fila, int col)
{
    int indice = col / 4;       // que muestra es
    int resto = col % 4;        // 3 = columna de separacion (no es muestra)

    if (indice < NUM_COLORES && resto < 3 && fila >= 1 && fila <= 3)
    {
        colorActual = indice;
        actualizarBufferPaleta();
        actualizarTitulo(window);
    }
}

// ----------------------------- CALLBACKS -------------------------------

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    // Clic derecho: cierra el poligono en curso
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS && herramientaActual == HERRAMIENTA_POLIGONO)
            cerrarPoligono();
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    if (action == GLFW_PRESS)
    {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        int fila, col;
        pixelACelda(window, xpos, ypos, fila, col);

        // Clic en la barra de la paleta: elegir color y no dibujar
        if (fila < FILAS_PALETA)
        {
            seleccionarColorEnPaleta(window, fila, col);
            return;
        }

        // Poligono libre: cada clic agrega un vertice (sin repetir el ultimo)
        if (herramientaActual == HERRAMIENTA_POLIGONO)
        {
            bool repetido = !verticesPoligono.empty()
                         && verticesPoligono.back().fila == fila
                         && verticesPoligono.back().col == col;

            if (!repetido)
                verticesPoligono.push_back({ fila, col });

            cursorFila = fila;
            cursorCol = col;
            actualizarVistaPoligono();
            return;
        }

        dibujando = true;
        filaInicio = fila;
        colInicio = col;
        ultimaFila = fila;
        ultimaCol = col;

        trazoPincel = { { fila, col } };
        vistaPrevia = engrosar({ { fila, col } }, tamanoPincel);
        actualizarBufferPrevia();
    }
    else if (action == GLFW_RELEASE && dibujando)
    {
        dibujando = false;
        confirmarFigura();
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    int filaFin, colFin;
    pixelACelda(window, xpos, ypos, filaFin, colFin);

    // El mouse no puede entrar a la paleta mientras se dibuja
    if (filaFin < FILAS_PALETA)
        filaFin = FILAS_PALETA;

    // Poligono libre: la vista previa sigue al mouse aunque no haya clic presionado
    if (herramientaActual == HERRAMIENTA_POLIGONO)
    {
        cursorFila = filaFin;
        cursorCol = colFin;
        if (!verticesPoligono.empty())
            actualizarVistaPoligono();
        return;
    }

    if (!dibujando)
        return;

    if (herramientaActual == HERRAMIENTA_PINCEL)
    {
        // Trazo libre: se unen las celdas por donde pasa el mouse con lineas de Bresenham
        if (filaFin == ultimaFila && colFin == ultimaCol)
            return;

        agregarCeldas(trazoPincel, bresenhamLinea(ultimaCol, ultimaFila, colFin, filaFin));
        ultimaFila = filaFin;
        ultimaCol = colFin;
        vistaPrevia = engrosar(trazoPincel, tamanoPincel);
    }
    else
    {
        // Figura (linea, triangulo, cuadrado o circulo) con el grosor y relleno elegidos
        vistaPrevia = engrosar(calcularFigura(herramientaActual, colInicio, filaInicio, colFin, filaFin, rellenoActivo), tamanoPincel);
    }

    actualizarBufferPrevia();
}

// La rueda del mouse cambia el tamano del pincel
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    cambiarTamano(window, yoffset > 0 ? 1 : -1);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_RELEASE)
        return;

    // El tamano acepta tecla mantenida (REPEAT); el resto solo al presionar
    switch (key)
    {
    case GLFW_KEY_UP:
    case GLFW_KEY_KP_ADD:
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_RIGHT_BRACKET:    // la tecla "+" de un teclado en espanol
        cambiarTamano(window, 1);
        return;
    case GLFW_KEY_DOWN:
    case GLFW_KEY_KP_SUBTRACT:
    case GLFW_KEY_MINUS:
    case GLFW_KEY_SLASH:            // la tecla "-" de un teclado en espanol
        cambiarTamano(window, -1);
        return;
    }

    if (action != GLFW_PRESS)
        return;

    switch (key)
    {
    case GLFW_KEY_G:
        mostrarGrilla = !mostrarGrilla;   // alternar grilla
        actualizarTitulo(window);
        break;
    case GLFW_KEY_F:
        rellenoActivo = !rellenoActivo;   // alternar relleno
        actualizarTitulo(window);
        if (herramientaActual == HERRAMIENTA_POLIGONO)
            actualizarVistaPoligono();
        break;
    case GLFW_KEY_C:
        limpiarLienzo();
        break;
    case GLFW_KEY_1:
        seleccionarHerramienta(window, HERRAMIENTA_LINEA);
        break;
    case GLFW_KEY_2:
        seleccionarHerramienta(window, HERRAMIENTA_TRIANGULO);
        break;
    case GLFW_KEY_3:
        seleccionarHerramienta(window, HERRAMIENTA_CUADRADO);
        break;
    case GLFW_KEY_4:
        seleccionarHerramienta(window, HERRAMIENTA_CIRCULO);
        break;
    case GLFW_KEY_5:
        seleccionarHerramienta(window, HERRAMIENTA_PINCEL);
        break;
    case GLFW_KEY_6:
        seleccionarHerramienta(window, HERRAMIENTA_POLIGONO);
        break;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        if (herramientaActual == HERRAMIENTA_POLIGONO)
            cerrarPoligono();
        break;
    case GLFW_KEY_BACKSPACE:
        if (herramientaActual == HERRAMIENTA_POLIGONO && !verticesPoligono.empty())
        {
            verticesPoligono.pop_back();
            actualizarVistaPoligono();
        }
        break;
    case GLFW_KEY_ESCAPE:
        if (!verticesPoligono.empty())
            cancelarPoligono();                  // primero cancela el poligono en curso
        else
            glfwSetWindowShouldClose(window, true);
        break;
    }
}

// ------------------------------- MAIN ----------------------------------
int main()
{
    if (!glfwInit())
    {
        std::cerr << "Error al inicializar GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(ANCHO_VENTANA, ALTO_VENTANA, "Mini Paint", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Error al crear la ventana\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Error al inicializar GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    unsigned int programa = crearPrograma();

    // Cuatro buffers: grilla (fija), lienzo, vista previa y paleta
    crearBuffer(VAOGrilla, VBOGrilla);
    crearBuffer(VAOLienzo, VBOLienzo);
    crearBuffer(VAOPrevia, VBOPrevia);
    crearBuffer(VAOPaleta, VBOPaleta);

    verticesGrilla = subirDatos(VBOGrilla, crearVerticesGrilla(), GL_STATIC_DRAW);
    actualizarBufferPaleta();
    actualizarTitulo(window);

    while (!glfwWindowShouldClose(window))
    {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);   // fondo blanco
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(programa);

        // 1) Celdas ya confirmadas (cada una con su color)
        if (verticesLienzo > 0)
        {
            glBindVertexArray(VAOLienzo);
            glDrawArrays(GL_TRIANGLES, 0, verticesLienzo);
        }

        // 2) Vista previa de la figura en curso (con el color elegido)
        if (verticesPrevia > 0)
        {
            glBindVertexArray(VAOPrevia);
            glDrawArrays(GL_TRIANGLES, 0, verticesPrevia);
        }

        // 3) Grilla encima, solo si esta activada
        if (mostrarGrilla)
        {
            glBindVertexArray(VAOGrilla);
            glDrawArrays(GL_LINES, 0, verticesGrilla);
        }

        // 4) Barra de la paleta
        glBindVertexArray(VAOPaleta);
        glDrawArrays(GL_TRIANGLES, 0, verticesPaleta);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAOGrilla);
    glDeleteBuffers(1, &VBOGrilla);
    glDeleteVertexArrays(1, &VAOLienzo);
    glDeleteBuffers(1, &VBOLienzo);
    glDeleteVertexArrays(1, &VAOPrevia);
    glDeleteBuffers(1, &VBOPrevia);
    glDeleteVertexArrays(1, &VAOPaleta);
    glDeleteBuffers(1, &VBOPaleta);
    glDeleteProgram(programa);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}