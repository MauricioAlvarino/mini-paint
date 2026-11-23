# mini-paint
MAURICIO ENRIQUE ALVARINO MERCADO
6014376
Un programa de dibujo en C++ que usa OpenGL 3.3 (GLFW + GLAD). Lo particular es que **no dibuja con las funciones de líneas o círculos de OpenGL**: cada figura se calcula a mano con algoritmos clásicos de gráficos por computador (Bresenham, punto medio y scanline), y OpenGL solo se usa para pintar el resultado en pantalla.

## La idea central: un lienzo hecho de celdas

El lienzo no es una imagen con píxeles reales, sino una **cuadrícula de 40 × 30 celdas**, y cada celda funciona como un "píxel gigante" de 18 × 18 píxeles de pantalla. Debajo del lienzo hay una barra de 5 filas reservada para la paleta de colores.

Todo el programa gira alrededor de esa cuadrícula:

- El **lienzo** es un arreglo donde cada celda guarda un número: `0` significa vacía y `k > 0` significa pintada con el color `k - 1` de la paleta.
- Cada figura se calcula como una **lista de celdas** (fila, columna).
- Dibujar es, al final, decidir qué celdas pintar y de qué color.

La grilla visible (tecla `G`) es solo decoración. El lienzo siempre está dividido en celdas, se vea la grilla o no.

## Cómo está organizado el código

Todo vive en un solo archivo, `main.cpp`, pero está separado en capas que conviene leer en este orden:

1. **Shaders.** Un vertex shader y un fragment shader mínimos. Cada vértice trae posición y color, y el fragment shader simplemente pinta con ese color.
2. **Configuración y tipos.** Constantes del lienzo (columnas, filas, tamaño de ventana), las estructuras `Celda` y `Color`, el enum de herramientas y la paleta de 10 colores.
3. **Geometría básica.** Funciones que convierten celdas en rectángulos (dos triángulos por celda) en coordenadas normalizadas de OpenGL, además de la generación de las líneas de la grilla.
4. **Algoritmos de rasterizado.** El corazón del proyecto (se detalla abajo).
5. **Buffers y estado del dibujo.** Funciones que suben los datos a la GPU cada vez que algo cambia.
6. **Callbacks de entrada.** Ratón, rueda y teclado.
7. **`main()`.** Crea la ventana, inicializa OpenGL y ejecuta el bucle de render.

## Los algoritmos

| Herramienta | Cómo se calcula |
|---|---|
| **Línea** | Algoritmo de Bresenham, que usa solo sumas y restas de enteros para decidir qué celda sigue. |
| **Triángulo** | Tres líneas de Bresenham: la base en la fila donde se hizo clic y la punta en la fila del mouse. |
| **Cuadrado** | Cuatro líneas de Bresenham. El lado es la mayor distancia, horizontal o vertical, entre el clic y el mouse. |
| **Círculo** | Algoritmo del punto medio: se calcula un solo octante y los otros siete salen por simetría. |
| **Pincel** | Se unen con Bresenham las celdas por donde pasa el mouse, para que el trazo nunca quede con huecos. |
| **Relleno** | Scanline: en cada fila se busca la celda más a la izquierda y la más a la derecha del contorno y se pinta lo que hay en medio. Sirve para figuras convexas. |
| **Polígono libre** | Cada clic agrega un vértice. El relleno usa scanline con regla par-impar, así que también funciona con formas cóncavas. |

El **tamaño del pincel** no es una herramienta aparte: es un paso final que "engorda" cualquier figura convirtiendo cada celda en un cuadrado de N × N celdas, sin repetir ninguna.

## Vista previa y confirmación

Cuando arrastras el mouse, la figura no se guarda todavía. Vive en una **vista previa** que se recalcula en cada movimiento. Solo cuando sueltas el clic (o cierras el polígono) esas celdas se copian al lienzo definitivo con el color elegido. Por eso puedes estirar, encoger y mover una figura antes de decidirte.

## Cómo se dibuja en pantalla

El programa mantiene **cuatro buffers** en la GPU, cada uno con su VAO y VBO:

- **Grilla:** se crea una sola vez.
- **Lienzo:** se reconstruye al confirmar una figura o limpiar.
- **Vista previa:** se actualiza mientras dibujas.
- **Paleta:** se actualiza al cambiar de color.

En cada cuadro se dibujan en este orden: lienzo, vista previa, grilla (si está activa) y paleta. La paleta va al final para quedar siempre encima.

## Entrada del usuario

Los eventos se manejan con callbacks de GLFW. El ratón convierte la posición en píxeles a una celda (invirtiendo el eje Y, porque en pantalla crece hacia abajo y en OpenGL hacia arriba). Un clic en la barra inferior elige color en lugar de dibujar, y mientras se dibuja el mouse no puede entrar a la paleta.

## Controles

| Tecla / acción | Efecto |
|---|---|
| `1` a `6` | Línea, triángulo, cuadrado, círculo, pincel, polígono libre |
| Clic + arrastrar | Dibuja la figura con vista previa |
| Clic en la paleta | Cambia el color (el blanco funciona como borrador) |
| `↑` / `↓`, rueda, `+` / `-` | Cambia el tamaño del pincel (1 a 8 celdas) |
| `F` | Activa o desactiva el relleno |
| Polígono: clic | Agrega un vértice |
| Polígono: `Enter` o clic derecho | Cierra y guarda el polígono |
| Polígono: `Backspace` / `Esc` | Quita el último vértice / cancela |
| `G` | Muestra u oculta la grilla |
| `C` | Limpia el lienzo |
| `Esc` | Sale del programa |

## Requisitos

- Compilador de C++ con soporte para C++11 o superior
- [GLFW](https://www.glfw.org/)
- [GLAD](https://gen.glad.sh/) (OpenGL 3.3 Core)

## Historia del proyecto

El repositorio avanza en cinco commits, cada uno con el programa completo y funcional hasta ese punto:

1. Ventana OpenGL, grilla, lienzo por celdas y línea Bresenham
2. Triángulo, cuadrado y círculo
3. Pincel, paleta de colores y tamaño de pincel
4. Relleno de primitivas
5. Forma libre: polígono con relleno opcional
