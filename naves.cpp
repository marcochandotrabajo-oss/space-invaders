#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>

// **********************************************
// 1. CONSTANTES Y CONFIGURACIÓN DE VENTANA (800x600)
// **********************************************
const int VENTANA_ANCHO = 800;
const int VENTANA_ALTO = 600;
const std::string RUTA_RECORDS = "records.txt";
const size_t MAX_RECORDS = 10;
const DWORD TIEMPO_PANTALLA = 30000; // 30 segundos de espera por pantalla estática

HWND hwndGlobal = NULL;
std::string pistaActual = "Silenciada";

// Máquina de estados para controlar las diferentes pantallas del juego
enum EstadoJuego {
    ESTADO_PRESENTACION = 0,   // Pantalla 0: Nave detallada con musica4.wav[cite: 1]
    ESTADO_REGISTRO_NOMBRE,    // Pantalla 1: Ingreso de nombre por teclado
    ESTADO_SELECCION_MUSICA,   // Selección de pistas 1-4 o silencio (0)
    ESTADO_JUEGO,              // Partida arcade de Space Invaders
    ESTADO_GAMEOVER,           // Pantalla de derrota con gameover.wav (suena 1 vez)
    ESTADO_RECORDS             // Sala de récords con fechas del PC
};

EstadoJuego estadoActual = ESTADO_PRESENTACION;

// Estructura para almacenar las puntuaciones, nombres y fechas/horas del PC
struct RecordEntry {
    int puntuacion;
    std::string nombre;
    std::string fechaHora;

    bool operator>(const RecordEntry& otra) const {
        return puntuacion > otra.puntuacion;
    }
};

class Bala {
public:
    int x, y;
    bool activa = true;
    Bala(int px, int py) : x(px), y(py) {}
};

class Enemigo {
public:
    int x, y;
    bool vivo = true;
    Enemigo(int px, int py) : x(px), y(py) {}
};

// Variables globales para la partida actual y entrada de texto
std::string nombreJugadorTemp = "";
int puntuacionFinalActual = 0;
DWORD tiempoInicioPantalla = 0;
char bufferNombre[11] = {0};
int indiceNombre = 0;

// Variables dinámicas del juego activo
int jugadorX = VENTANA_ANCHO / 2;
int jugadorY = VENTANA_ALTO - 100;
int vidas = 3;
int puntuacion = 0;
std::vector<Bala> balas;
std::vector<Enemigo> enemigos;
int contadorEnemigos = 0;
int velocidadEnemigos = 20;

// **********************************************
// 2. GESTIÓN DE MÚSICA Y RÉCORDS (Con fecha de PC)
// **********************************************
void gestionarMusica(int opcion) {
    if (opcion >= 1 && opcion <= 4) {
        std::string archivo = "musica" + std::to_string(opcion) + ".wav";
        // Reproduce en bucle asíncrono
        BOOL resultado = PlaySoundA(archivo.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_LOOP);
        if (resultado) pistaActual = archivo;
        else pistaActual = "No se encuentra " + archivo;
    } else if (opcion == 0) {
        PlaySoundA(NULL, 0, 0); // Silencia la música
        pistaActual = "Silenciada";
    }
}

// Reproduce la música de derrota una sola vez (sin SND_LOOP)
void reproducirGameOver() {
    PlaySoundA(NULL, 0, 0); 
    PlaySoundA("gameover.wav", NULL, SND_FILENAME | SND_ASYNC);
}

// Obtiene la fecha y hora exacta del PC para asociarla al récord
std::string obtenerFechaHoraActual() {
    time_t ahora = time(nullptr);
    tm tiempoLocal;
    localtime_s(&tiempoLocal, &ahora);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &tiempoLocal);
    return std::string(buffer);
}

// Carga, ordena y guarda la lista de los 10 mejores récords en archivo local
std::vector<RecordEntry> cargarRecords() {
    std::vector<RecordEntry> records;
    std::ifstream archivo(RUTA_RECORDS);
    if (archivo.is_open()) {
        int p;
        std::string n, f, h;
        while (archivo >> p >> n >> f >> h) {
            records.push_back({p, n, f + " " + h});
        }
        archivo.close();
    }
    std::sort(records.begin(), records.end(), std::greater<RecordEntry>());
    return records;
}

void guardarRecords(const std::vector<RecordEntry>& records) {
    std::ofstream archivo(RUTA_RECORDS);
    if (archivo.is_open()) {
        for (const auto& r : records) {
            archivo << r.puntuacion << " " << r.nombre << " " << r.fechaHora << "\n";
        }
        archivo.close();
    }
}

void registrarPuntuacion(int nuevaPuntuacion, const std::string& nombreJugador) {
    auto records = cargarRecords();
    std::string fechaActual = obtenerFechaHoraActual();
    records.push_back({nuevaPuntuacion, nombreJugador, fechaActual});
    std::sort(records.begin(), records.end(), std::greater<RecordEntry>());
    if (records.size() > MAX_RECORDS) records.resize(MAX_RECORDS);
    guardarRecords(records);
}

// **********************************************
// 3. LOGICA DE CICLOS Y REINICIO
// **********************************************
void reiniciarCicloJuego() {
    nombreJugadorTemp = "";
    memset(bufferNombre, 0, sizeof(bufferNombre));
    indiceNombre = 0;
    gestionarMusica(4); // Suena obligatoriamente musica4.wav en la presentación[cite: 1]
    tiempoInicioPantalla = GetTickCount();
    estadoActual = ESTADO_PRESENTACION;
}

void iniciarPartidaReal() {
    vidas = 3;
    puntuacion = 0;
    jugadorX = VENTANA_ANCHO / 2;
    jugadorY = VENTANA_ALTO - 100;
    balas.clear();
    enemigos.clear();
    contadorEnemigos = 0;
    estadoActual = ESTADO_JUEGO;
}

// **********************************************
// 4. RENDERIZADO GRÁFICO CON DOBLE BÚFER (Sin parpadeo)
// **********************************************
void renderizarJuego(HDC hdc) {
    // Creación de búfer de memoria para evitar parpadeos visuales
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, VENTANA_ANCHO, VENTANA_ALTO);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // Fondo negro espacial
    RECT rect = { 0, 0, VENTANA_ANCHO, VENTANA_ALTO };
    HBRUSH brushNegro = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rect, brushNegro);
    DeleteObject(brushNegro);

    SetTextColor(hdcMem, RGB(0, 255, 0));
    SetBkMode(hdcMem, TRANSPARENT);
    HFONT hFont = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, "Consolas");
    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

    // Renderizado según la pantalla activa actual
    if (estadoActual == ESTADO_PRESENTACION) {
        TextOutA(hdcMem, 280, 50, "=== SPACE INVADERS ===", 23);
        
        // Arte ASCII de la nave detallada[cite: 1]
        TextOutA(hdcMem, 380, 150, "|", 1);
        TextOutA(hdcMem, 380, 170, "|", 1);
        TextOutA(hdcMem, 360, 190, "/|\\", 3);
        TextOutA(hdcMem, 350, 210, "/ | \\", 5);
        TextOutA(hdcMem, 300, 230, "[#]  /  |  \\  [#]", 17);
        TextOutA(hdcMem, 300, 250, "| | /  / \\  \\ | |", 17);
        TextOutA(hdcMem, 300, 270, "| |/__/   \\__\\| |", 17);
        TextOutA(hdcMem, 300, 290, "|___/  /|\\  \\___|", 17);
        TextOutA(hdcMem, 355, 310, "|   v   |", 9);

        TextOutA(hdcMem, 250, 420, "Iniciando sistemas de la nave...", 31);
        TextOutA(hdcMem, 240, 520, "(Sali con 7 | Reinicia con 6)", 28);

        // Transición automática tras 30 segundos
        if (GetTickCount() - tiempoInicioPantalla >= TIEMPO_PANTALLA) {
            tiempoInicioPantalla = GetTickCount();
            estadoActual = ESTADO_REGISTRO_NOMBRE;
        }
    }
    else if (estadoActual == ESTADO_REGISTRO_NOMBRE) {
        TextOutA(hdcMem, 250, 150, "=== REGISTRO DE JUGADOR ===", 27);
        TextOutA(hdcMem, 200, 220, "Ingresa tu nombre (max 10 chars) y Enter:", 41);
        
        std::string txtNombre = "Nombre: " + std::string(bufferNombre) + "_";
        TextOutA(hdcMem, 300, 280, txtNombre.c_str(), (int)txtNombre.length());
        TextOutA(hdcMem, 240, 520, "(Sali con 7 | Reinicia con 6)", 28);
    }
    else if (estadoActual == ESTADO_SELECCION_MUSICA) {
        TextOutA(hdcMem, 230, 120, "=== SELECCIONA TU MUSICA ===", 27);
        TextOutA(hdcMem, 250, 180, "1. Musica 1 (musica1.wav)", 24);
        TextOutA(hdcMem, 250, 210, "2. Musica 2 (musica2.wav)", 24);
        TextOutA(hdcMem, 250, 240, "3. Musica 3 (musica3.wav)", 24);
        TextOutA(hdcMem, 250, 270, "4. Musica 4 (musica4.wav)", 24);
        TextOutA(hdcMem, 250, 300, "0. Jugar sin musica", 19);
        TextOutA(hdcMem, 240, 520, "(Sali con 7 | Reinicia con 6)", 28);
    }
    else if (estadoActual == ESTADO_JUEGO) {
        // Movimiento horizontal de la nave con teclado
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) if (jugadorX > 50) jugadorX -= 6;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) if (jugadorX < VENTANA_ANCHO - 50) jugadorX += 6;

        // Generación automática de enemigos
        contadorEnemigos++;
        if (contadorEnemigos >= velocidadEnemigos) {
            int xRand = 50 + (rand() % (VENTANA_ANCHO - 100));
            enemigos.push_back(Enemigo(xRand, 50));
            contadorEnemigos = 0;
        }

        // Lógica de colisiones enemigos
        for (auto& e : enemigos) {
            e.y += 3;
            if (e.y >= VENTANA_ALTO - 80) e.vivo = false;
            if (abs(e.x - jugadorX) < 25 && abs(e.y - jugadorY) < 25) {
                vidas--;
                e.vivo = false;
                if (vidas <= 0) {
                    registrarPuntuacion(puntuacion, nombreJugadorTemp.empty() ? "Anonimo" : nombreJugadorTemp);
                    puntuacionFinalActual = puntuacion;
                    reproducirGameOver();
                    tiempoInicioPantalla = GetTickCount();
                    estadoActual = ESTADO_GAMEOVER;
                }
            }
        }

        // Lógica de colisiones de balas
        for (auto& b : balas) {
            b.y -= 10;
            if (b.y < 50) b.activa = false;
            for (auto& e : enemigos) {
                if (e.vivo && b.activa && abs(b.x - e.x) < 15 && abs(b.y - e.y) < 15) {
                    e.vivo = false;
                    b.activa = false;
                    puntuacion += 10;
                }
            }
        }

        enemigos.erase(std::remove_if(enemigos.begin(), enemigos.end(), [](const Enemigo& e) { return !e.vivo; }), enemigos.end());
        balas.erase(std::remove_if(balas.begin(), balas.end(), [](const Bala& b) { return !b.activa; }), balas.end());

        // Dibujar marco del campo de juego
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 255, 0));
        HGDIOBJ hOldPen = SelectObject(hdcMem, hPen);
        Rectangle(hdcMem, 40, 40, VENTANA_ANCHO - 40, VENTANA_ALTO - 60);
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hPen);

        // Dibujar nave del jugador y elementos activos
        TextOutA(hdcMem, jugadorX, jugadorY - 20, "^", 1);
        TextOutA(hdcMem, jugadorX - 10, jugadorY, "/ | \\", 5);
        TextOutA(hdcMem, jugadorX - 20, jugadorY + 20, "[A]", 3);

        for (const auto& b : balas) {
            TextOutA(hdcMem, b.x, b.y, "|", 1);
        }
        for (const auto& e : enemigos) {
            TextOutA(hdcMem, e.x, e.y, "[V]", 3);
        }

        std::string infoHUD = "Puntos: " + std::to_string(puntuacion) + " | Vidas: " + std::to_string(vidas) + " | Musica: [" + pistaActual + "]";
        TextOutA(hdcMem, 50, VENTANA_ALTO - 50, infoHUD.c_str(), (int)infoHUD.length());
        TextOutA(hdcMem, 500, VENTANA_ALTO - 50, "(Sali con 7 | Reinicia con 6)", 28);
    }
    else if (estadoActual == ESTADO_GAMEOVER) {
        TextOutA(hdcMem, 340, 100, "GAME OVER", 9);
        std::string infoFin = "Jugador: " + nombreJugadorTemp + "  |  Puntuacion: " + std::to_string(puntuacionFinalActual);
        TextOutA(hdcMem, 200, 200, infoFin.c_str(), (int)infoFin.length());
        TextOutA(hdcMem, 240, 520, "(Sali con 7 | Reinicia con 6)", 28);

        if (GetTickCount() - tiempoInicioPantalla >= TIEMPO_PANTALLA) {
            tiempoInicioPantalla = GetTickCount();
            estadoActual = ESTADO_RECORDS;
        }
    }
    else if (estadoActual == ESTADO_RECORDS) {
        TextOutA(hdcMem, 320, 50, "SALA DE RECORDS", 15);
        auto records = cargarRecords();
        int yOffset = 120;
        for (size_t i = 0; i < records.size() && i < 8; ++i) {
            std::string lin = std::to_string(i + 1) + ". " + records[i].nombre + "   " + std::to_string(records[i].puntuacion) + " pts   [" + records[i].fechaHora + "]";
            TextOutA(hdcMem, 150, yOffset, lin.c_str(), (int)lin.length());
            yOffset += 40;
        }
        TextOutA(hdcMem, 240, 520, "(Sali con 7 | Reinicia con 6)", 28);

        if (GetTickCount() - tiempoInicioPantalla >= TIEMPO_PANTALLA) {
            reiniciarCicloJuego();
        }
    }

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);

    // Volcado del búfer a pantalla para fluidez total
    BitBlt(hdc, 0, 0, VENTANA_ANCHO, VENTANA_ALTO, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
}

// **********************************************
// 5. PROCEDIMIENTO DE VENTANA (Eventos de Teclado)
// **********************************************
LRESULT CALLBACK TratarVentana(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CHAR:
            if (estadoActual == ESTADO_REGISTRO_NOMBRE) {
                if (wParam == VK_RETURN) {
                    if (strlen(bufferNombre) > 0) nombreJugadorTemp = std::string(bufferNombre);
                    else nombreJugadorTemp = "Anonimo";
                    estadoActual = ESTADO_SELECCION_MUSICA;
                } else if (wParam == VK_BACK) {
                    if (indiceNombre > 0) {
                        bufferNombre[--indiceNombre] = '\0';
                    }
                } else if (wParam >= 32 && wParam <= 126 && indiceNombre < 10) {
                    bufferNombre[indiceNombre++] = (char)wParam;
                    bufferNombre[indiceNombre] = '\0';
                }
            }
            else if (estadoActual == ESTADO_SELECCION_MUSICA) {
                if (wParam >= '0' && wParam <= '4') {
                    int sel = wParam - '0';
                    gestionarMusica(sel);
                    iniciarPartidaReal();
                }
            }
            break;

        case WM_KEYDOWN:
            // Control global: Tecla '7' para salir completamente del juego
            if (wParam == '7') {
                PlaySoundA(NULL, 0, 0);
                exit(0);
            }
            // Control global: Tecla '6' para reiniciar al inicio instantáneamente
            if (wParam == '6') {
                PlaySoundA(NULL, 0, 0);
                reiniciarCicloJuego();
            }
            // Disparo con Barra Espaciadora durante la partida
            if (estadoActual == ESTADO_JUEGO && wParam == VK_SPACE) {
                balas.push_back(Bala(jugadorX, jugadorY - 10));
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// **********************************************
// 6. PUNTO DE ENTRADA GRÁFICO (WinMain - Centrado a 800x600 Fijos)
// **********************************************
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand(static_cast<unsigned int>(time(nullptr)));

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = TratarVentana;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SpaceInvadersWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) return 0;

    // Calcula el tamaño real para que el área cliente sea exactamente de 800x600 píxeles
    RECT rectVentana = { 0, 0, VENTANA_ANCHO, VENTANA_ALTO };
    AdjustWindowRect(&rectVentana, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int anchoReal = rectVentana.right - rectVentana.left;
    int altoReal = rectVentana.bottom - rectVentana.top;

    // Centra la ventana automáticamente en el monitor del usuario
    int pantallaAncho = GetSystemMetrics(SM_CXSCREEN);
    int pantallaAlto = GetSystemMetrics(SM_CYSCREEN);
    int posX = (pantallaAncho - anchoReal) / 2;
    int posY = (pantallaAlto - altoReal) / 2;

    // Crea la ventana bloqueada (sin maximizar ni cambiar tamaño)
    hwndGlobal = CreateWindowExA(
        0,
        "SpaceInvadersWindowClass",
        "Space Invaders - 800x600 Fixed",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY,
        anchoReal, altoReal,
        NULL, NULL, hInstance, NULL
    );

    if (!hwndGlobal) return 0;

    ShowWindow(hwndGlobal, nCmdShow);
    UpdateWindow(hwndGlobal);

    reiniciarCicloJuego();

    bool ejecutando = true;
    MSG msg = {0};

    // Bucle principal de mensajes y refresco gráfico de la ventana
    while (ejecutando) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) ejecutando = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        HDC hdc = GetDC(hwndGlobal);
        renderizarJuego(hdc);
        ReleaseDC(hwndGlobal, hdc);

        Sleep(25); // Control de velocidad y tasa de refresco fluida
    }

    return (int)msg.wParam;
}