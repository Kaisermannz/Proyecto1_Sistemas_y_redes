#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>

// Función para obtener la hora actual en formato string
std::string obtener_hora_actual() {
    auto t = std::time(nullptr); 
    auto tm = *std::localtime(&t); 
    std::ostringstream oss; 
    oss << std::put_time(&tm, "%H:%M:%S"); 
    return oss.str(); 
}

class Buffer {
private:
    int capacidad; 
    std::vector<std::string> buffer; 
    int frente; 
    int fondo; 
    int count; 
    std::mutex mtx; 
    std::condition_variable buffer_lleno;
    std::condition_variable buffer_vacio;
public:
    // Constructor: Inicializa el buffer con la capacidad especificada
    // También inicializa los índices `frente` y `fondo` a 0, y `count` a 0
    Buffer(int cap) : capacidad(cap), frente(0), fondo(0), count(0) {
        buffer.resize(capacidad); // Redimensiona el vector al tamaño del buffer
    }

    // Inserta un dato en el buffer si hay espacio disponible
    // Si el buffer está lleno, no se bloquea, simplemente sale de la función
    void insertar(const std::string& dato) {
        std::unique_lock<std::mutex> lock(mtx); 
        if (count == capacidad) {
            return;  // Buffer lleno, el productor no se bloquea
        }
        buffer[fondo] = dato; 
        fondo = (fondo + 1) % capacidad; 
        ++count; 
        buffer_vacio.notify_one(); // Notifica a los consumidores que el buffer ya no está vacío
    }

    // Extrae un dato del buffer si no está vacío
    // Si el buffer está vacío, el consumidor se bloquea hasta que haya datos
    std::string extraer() {
        std::unique_lock<std::mutex> lock(mtx); 
        buffer_vacio.wait(lock, [this] { return count > 0; }); // Espera hasta que el buffer tenga al menos un dato
        std::string dato = buffer[frente]; // Extrae el dato de la posición `frente`
        frente = (frente + 1) % capacidad; // Avanza el índice `frente` circularmente
        --count; 
        buffer_lleno.notify_one(); // Notifica a los productores que el buffer ya no está lleno
        return dato;
    }

    // Verifica si el buffer está lleno

    bool esta_lleno() {
        std::lock_guard<std::mutex> lock(mtx);
        return count == capacidad; 
    }
};

class Productor {
private:
    int id; 
    Buffer& buffer; 
    int num_producciones; 
    std::ofstream& log_file; 

public:
    // Constructor: Inicializa el productor con su ID, referencia al buffer, número de producciones y archivo de log
    Productor(int i, Buffer& buf, int np, std::ofstream& log) 
        : id(i), buffer(buf), num_producciones(np), log_file(log) {}

    // Método principal del productor: genera datos y los intenta insertar en el buffer
    void producir() {
        for (int i = 1; i <= num_producciones; ++i) {
            std::string dato = std::to_string(id) + "_" + std::to_string(i); // Genera el dato en formato "id_numero"

            // Escribe en el log que ha generado un nuevo dato
            log_file << obtener_hora_actual() << " - Productor " << id << ": Generó el dato '" << dato << "'" << std::endl;
            
            while (true) {
                if (!buffer.esta_lleno()) { 
                    buffer.insertar(dato);
                    log_file << obtener_hora_actual() << " - Productor " << id << ": Insertó exitosamente el dato '" << dato << "' en el buffer" << std::endl;
                    break; // Sale del bucle al insertar el dato
                } else {
                    // Si el buffer está lleno, escribe en el log que no pudo insertar el dato
                    log_file << obtener_hora_actual() << " - Productor " << id << ": Buffer lleno. No pudo insertar el dato '" << dato << "'. Reintentará después." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(std::rand() % 5)); // Espera un tiempo aleatorio antes de reintentar
                }
            }
            
            // Genera un tiempo de espera aleatorio antes de la siguiente producción
            int tiempo_espera = std::rand() % 6;
            log_file << obtener_hora_actual() << " - Productor " << id << ": Esperará " << tiempo_espera << " segundos antes de la siguiente producción" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(tiempo_espera)); // Espera el tiempo generado
        }
    }
};

class Consumidor {
private:
    int id; 
    Buffer& buffer;
    int num_consumos;
    std::ofstream& log_file;

public:
    // Constructor: Inicializa el consumidor con su ID, referencia al buffer, número de consumos y archivo de log
    Consumidor(int i, Buffer& buf, int nc, std::ofstream& log) 
        : id(i), buffer(buf), num_consumos(nc), log_file(log) {}

    // Método principal del consumidor: intenta extraer datos del buffer
    void consumir() {
        for (int i = 0; i < num_consumos; ++i) {
            log_file << obtener_hora_actual() << " - Consumidor " << id << ": Intentando extraer un elemento del buffer" << std::endl;

            std::string dato = buffer.extraer(); // Extrae un dato del buffer
            
            log_file << obtener_hora_actual() << " - Consumidor " << id << ": Extrajo exitosamente el dato '" << dato << "' del buffer" << std::endl;
            
            // Genera un tiempo de espera aleatorio antes del siguiente consumo
            int tiempo_espera = std::rand() % 6;
            log_file << obtener_hora_actual() << " - Consumidor " << id << ": Esperará " << tiempo_espera << " segundos antes del siguiente consumo" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(tiempo_espera)); // Espera el tiempo generado
        }
    }
};

class Principal {
private:
    int NP, NC, BC, NPP, NCC; 
    Buffer buffer; 
    std::vector<std::thread> productores;
    std::vector<std::thread> consumidores;
    std::ofstream log_productor;
    std::ofstream log_consumidor;

public:
    // Constructor: Inicializa los parámetros principales y abre los archivos de log
    Principal(int np, int nc, int bc, int npp, int ncc)
        : NP(np), NC(nc), BC(bc), NPP(npp), NCC(ncc), buffer(BC) {
        log_productor.open("productor_log.txt"); // Abre el archivo de log de productores
        log_consumidor.open("consumidor_log.txt"); // Abre el archivo de log de consumidores
    }

    // Inicializa los productores y consumidores
    void inicializar() {
        // Crea los hilos de productores
        for (int i = 1; i <= NP; ++i) {
            log_productor << obtener_hora_actual() << " - Productor " << i << " creado" << std::endl;
            productores.emplace_back(&Productor::producir, Productor(i, buffer, NPP, log_productor)); // Crea y lanza un hilo para cada productor
        }

        // Crea los hilos de consumidores
        for (int i = 1; i <= NC; ++i) {
            log_consumidor << obtener_hora_actual() << " - Consumidor " << i << " creado" << std::endl;
            consumidores.emplace_back(&Consumidor::consumir, Consumidor(i, buffer, NCC, log_consumidor)); // Crea y lanza un hilo para cada consumidor
        }
    }

    // Ejecuta los productores y consumidores, y espera a que terminen
    void ejecutar() {
        // Espera a que todos los productores terminen su trabajo
        for (auto& p : productores) {
            p.join();
        }
        // Espera a que todos los consumidores terminen su trabajo
        for (auto& c : consumidores) {
            c.join();
        }

        // Registra que los productores han terminado
        for (int i = 1; i <= NP; ++i) {
            log_productor << obtener_hora_actual() << " - Productor " << i << " ha terminado" << std::endl;
        }
        // Registra que los consumidores han terminado
        for (int i = 1; i <= NC; ++i) {
            log_consumidor << obtener_hora_actual() << " - Consumidor " << i << " ha terminado" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Uso: " << argv[0] << " <NP> <NC> <BC> <NPP> <NCC>" << std::endl;
        return 1;
    }

    
    int NP = std::stoi(argv[1]);  // Número de productores
    int NC = std::stoi(argv[2]);  // Número de consumidores
    int BC = std::stoi(argv[3]);  // Capacidad del buffer
    int NPP = std::stoi(argv[4]); // Número de producciones por productor
    int NCC = std::stoi(argv[5]); // Número de consumos por consumidor

    // Validación de que haya suficiente producción para todos los consumidores
    if (NP * NPP < NC * NCC) {
        std::cerr << "No hay suficiente comida para los consumidores" << std::endl;
        return 1;
    }

    
    Principal principal(NP, NC, BC, NPP, NCC);
    principal.inicializar(); // Crea los hilos de productores y consumidores
    principal.ejecutar(); // Espera a que terminen todos los hilos

    return 0;
}
