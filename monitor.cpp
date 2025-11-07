// monitor.cpp
#include <bits/stdc++.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <regex>
using namespace std;
using Clock = chrono::steady_clock;
using namespace chrono;

struct Domain {
    string name;
    string status = "N/A";
    optional<double> latency_ms; // null -> no dato
    time_t last_check = 0;
    Clock::time_point next_check_due = Clock::now();
};

static const string DOMAINS_FILE = "domains.txt";
static const minutes CHECK_EVERY = minutes(10);

string now_str(time_t t) {
    if (t == 0) return "-";
    char buf[64];
    tm tmv{};
    localtime_r(&t, &tmv);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return string(buf);
}

// Ejecuta `ping -c 1 -W 2 host` y devuelve {up, latency_ms}
pair<bool, optional<double>> ping_once(const string& host) {
    // -c 1 -> 1 paquete ; -W 2 -> timeout 2s
    string cmd = "ping -c 1 -W 2 " + host + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {false, nullopt};
    string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    int rc = pclose(pipe);

    // Buscar 'time=XX ms'
    regex r(R"(time=([\d\.]+)\s*ms)");
    smatch m;
    if (regex_search(output, m, r)) {
        double ms = stod(m[1].str());
        return {true, ms};
    }
    // Si no hay time pero el rc es 0, lo marcamos como up (sin latencia)
    if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) {
        return {true, nullopt};
    }
    return {false, nullopt};
}

void save_domains(const vector<Domain>& v) {
    ofstream f(DOMAINS_FILE);
    for (auto& d : v) f << d.name << "\n";
}

vector<Domain> load_domains() {
    vector<Domain> v;
    ifstream f(DOMAINS_FILE);
    string s;
    while (getline(f, s)) {
        // limpiar espacios
        string t;
        for (char c: s) if (!isspace((unsigned char)c)) t += c;
        if (!t.empty()) {
            Domain d;
            d.name = t;
            d.next_check_due = Clock::now(); // forzar primera comprobación
            v.push_back(d);
        }
    }
    return v;
}

void clear_screen() {
    // ANSI clear
    cout << "\033[2J\033[H";
}

void print_table(const vector<Domain>& v) {
    cout << "Monit. dominios (ping cada " << CHECK_EVERY.count() << " min)\n";
    cout << "Archivo: " << DOMAINS_FILE << "   —   Añadir dominio: escribe abajo y ENTER   —   Salir: deja vacío y pulsa ENTER\n\n";

    // cabecera
    cout << left
         << setw(32) << "Dominio"
         << setw(10) << "Estado"
         << setw(14) << "Latencia(ms)"
         << setw(20) << "Último check"
         << "\n";
    cout << string(32+10+14+20, '-') << "\n";

    for (auto& d : v) {
        string lat = d.latency_ms.has_value() ? to_string(*d.latency_ms) : "-";
        cout << left
             << setw(32) << d.name.substr(0,31)
             << setw(10) << d.status
             << setw(14) << lat
             << setw(20) << now_str(d.last_check)
             << "\n";
    }
    cout << "\nAñadir dominio (ENTER para no añadir y refrescar): ";
    cout.flush();
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    vector<Domain> domains = load_domains();

    // Si no hay dominios, sugerimos algunos
    if (domains.empty()) {
        for (string d : {"example.com", "google.com"}) {
            domains.push_back(Domain{
                .name=d,
                .status="N/A",
                .latency_ms=nullopt,
                .last_check=0,
                .next_check_due=Clock::now()
            });
        }
        save_domains(domains);
    }

    while (true) {
        // Comprobaciones debidas
        auto now = Clock::now();
        for (auto& d : domains) {
            if (now >= d.next_check_due) {
                auto res = ping_once(d.name);
                d.last_check = time(nullptr);
                if (res.first) {
                    d.status = "UP";
                    d.latency_ms = res.second;
                } else {
                    d.status = "DOWN";
                    d.latency_ms = nullopt;
                }
                d.next_check_due = now + CHECK_EVERY;
            }
        }

        // Pintar
        clear_screen();
        print_table(domains);

        // Entrada de usuario no bloqueante larga complicaría; usamos una corta con timeout manual:
        // Leemos una línea con timeout "suave": damos ~3s para teclear, si no, refrescamos y seguimos.
        // Para simplificar, aquí: leemos con std::getline pero con un truco de tiempo limitado.
        // Implementación simple: ponemos cin.rdbuf()->in_avail() para ver si hay algo.
        // Si hay entrada, la tomamos; si no, dormimos 3s y refrescamos.
        bool had_input = false;
        for (int i=0; i<30; ++i) { // 3s en pasos de 100ms
            if (cin.rdbuf()->in_avail() > 0) { had_input = true; break; }
            this_thread::sleep_for(100ms);
        }

        if (had_input) {
            string newdom;
            getline(cin, newdom);
            // trim
            auto ltrim = [](string& s){ s.erase(s.begin(), find_if(s.begin(), s.end(), [](int ch){return !isspace(ch);})); };
            auto rtrim = [](string& s){ s.erase(find_if(s.rbegin(), s.rend(), [](int ch){return !isspace(ch);}).base(), s.end()); };
            ltrim(newdom); rtrim(newdom);

            if (!newdom.empty()) {
                // validar un poco (sin espacios, sin barra)
                if (newdom.find(' ') != string::npos || newdom.find('/') != string::npos) {
                    // ignorar entrada rara
                } else {
                    // ¿ya existe?
                    bool exists = any_of(domains.begin(), domains.end(), [&](const Domain& d){ return d.name == newdom; });
                    if (!exists) {
                        Domain d;
                        d.name = newdom;
                        d.status = "N/A";
                        d.latency_ms = nullopt;
                        d.last_check = 0;
                        d.next_check_due = Clock::now(); // comprobar al instante
                        domains.push_back(d);
                        save_domains(domains);
                    }
                }
            }
        } else {
            // sin entrada: seguimos
        }
    }

    return 0;
}
