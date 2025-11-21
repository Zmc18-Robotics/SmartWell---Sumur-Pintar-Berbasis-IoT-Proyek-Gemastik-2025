// Program Smart Well IoT dengan Kontrol Pompa dan Sistem Blokir
// Sensor range: 6-23cm, Tombol Pin 2 (Pompa), Pin 13 (Request Unblock)
// Mode: Aktif (bebas ambil air) dengan sistem blokir

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// WiFi
const char* namaWiFi = "Absolute Solver";
const char* sandiWiFi = "CynIsTheCutestBotEver3";

// Pin
const int pinTrig = 22, pinEcho = 23, pinBuzzer = 21;
const int pinTombolPompa = 2, pinRelay = 15;
const int pinTombolUnblock = 13; // Tombol untuk request unblock

// Sensor
const float jarakMin = 6.0, jarakMax = 22.0, tinggiAirEfektif = 16.0;
const float diameter = 6.5, volumeMax = 500.0;
const float luasAlas = 3.14159 * (diameter/2) * (diameter/2);

// Variabel global
float jarakTerukur, volumeAir, persentaseAir;
bool objekTerdeteksi = false, airTerdeteksi = false;
bool relayAktif = false;
bool pompaTerblokir = false; // Status blokir pompa
bool adaRequestUnblock = false; // Ada permintaan untuk unblock
int batasVolume = 100; // ml, batas air yang bisa diambil
int volumeAwalPompa = 0;
unsigned long waktuAlarm = 0, waktuSensor = 0;
unsigned long waktuRequestUnblock = 0; // Waktu request unblock
String statusSistem = "Standby";
String alasanBlokir = "Tidak ada"; // Alasan kenapa pompa diblokir

WebServer server(80);

// Deklarasi fungsi-fungsi
void bunyikanTone(int frekuensi, int durasi = 100);
void bacaSensor();
void cekTombol();
void hidupkanPompa();
void matikanPompa();
void cekBatasVolume();
void cekAlarm();
void blokirPompa(String alasan);
void unblokPompa();
void cekTombolUnblock();
void tanganiWeb();
void tanganiAPI();
void tanganiKontrol();

void setup() {
  pinMode(pinTrig, OUTPUT); pinMode(pinEcho, INPUT);
  pinMode(pinBuzzer, OUTPUT); pinMode(pinRelay, OUTPUT);
  pinMode(pinTombolPompa, INPUT_PULLUP);
  pinMode(pinTombolUnblock, INPUT_PULLUP); // Tombol unblock dengan pull-up
  digitalWrite(pinRelay, LOW);

  Serial.begin(115200);
  Serial.println("=== SMART WELL - Sumur Pintar berbasis IoT ===");
  bunyikanTone(300); delay(500);

  WiFi.begin(namaWiFi, sandiWiFi);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000); Serial.print(".");
  }
  Serial.println("\nWiFi terhubung: " + WiFi.localIP().toString());

  server.on("/", tanganiWeb);
  server.on("/api", tanganiAPI);
  server.on("/kontrol", tanganiKontrol);
  server.begin();
  Serial.println("Web server aktif!\n");
}

void loop() {
  server.handleClient();
  
  if (millis() - waktuSensor >= 500) {
    bacaSensor();
    waktuSensor = millis();
  }

  cekTombol();
  cekTombolUnblock(); // Cek tombol request unblock
  cekAlarm();
  cekBatasVolume();

  delay(100);
}

void bunyikanTone(int frekuensi, int durasi) {
  tone(pinBuzzer, frekuensi);
  delay(durasi);
  noTone(pinBuzzer);
}

void bacaSensor() {
  digitalWrite(pinTrig, LOW); delayMicroseconds(2);
  digitalWrite(pinTrig, HIGH); delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);
  long durasi = pulseIn(pinEcho, HIGH);
  jarakTerukur = (durasi * 0.034) / 2;

  if (jarakTerukur > 400 || jarakTerukur < 2) {
    objekTerdeteksi = false; airTerdeteksi = false;
    volumeAir = 0; persentaseAir = 0;
  } else {
    objekTerdeteksi = true;
    if (jarakTerukur >= jarakMin && jarakTerukur <= jarakMax) {
      airTerdeteksi = true;
      float tinggiAir = jarakMax - jarakTerukur;
      if (tinggiAir < 0) tinggiAir = 0;
      if (tinggiAir > tinggiAirEfektif) tinggiAir = tinggiAirEfektif;
      volumeAir = luasAlas * tinggiAir;
      persentaseAir = (tinggiAir / tinggiAirEfektif) * 100.0;
      if (persentaseAir > 100) persentaseAir = 100;
      if (volumeAir > volumeMax) volumeAir = volumeMax;
    } else {
      airTerdeteksi = false; volumeAir = 0; persentaseAir = 0;
    }
  }
}

void cekTombol() {
  static bool statusTombol2 = HIGH;
  bool baca2 = digitalRead(pinTombolPompa);

  // Tombol Pin 2 (Pompa)
  if (statusTombol2 == HIGH && baca2 == LOW) {
    if (pompaTerblokir) {
      // Pompa terblokir, berikan feedback
      statusSistem = "POMPA TERBLOKIR! Tekan tombol unblock.";
      // Bunyi peringatan blokir
      for (int i = 0; i < 2; i++) {
        bunyikanTone(800, 100);
        delay(150);
      }
      Serial.println("Pompa terblokir! Alasan: " + alasanBlokir);
    } else {
      hidupkanPompa();
    }
  }

  statusTombol2 = baca2;
}

void cekTombolUnblock() {
  static bool statusTombolUnblock = HIGH;
  bool bacaUnblock = digitalRead(pinTombolUnblock);

  // Tombol Pin 13 (Request Unblock)
  if (statusTombolUnblock == HIGH && bacaUnblock == LOW) {
    if (pompaTerblokir) {
      adaRequestUnblock = true;
      waktuRequestUnblock = millis();
      statusSistem = "REQUEST UNBLOCK - Menunggu konfirmasi monitor";
      
      // Bunyi konfirmasi request
      bunyikanTone(1000, 200);
      delay(100);
      bunyikanTone(1200, 200);
      
      Serial.println("Request unblock diterima! Menunggu konfirmasi dari web monitor.");
    } else {
      // Pompa tidak terblokir
      bunyikanTone(400, 100);
      Serial.println("Pompa tidak dalam status terblokir.");
    }
  }

  statusTombolUnblock = bacaUnblock;
}

void hidupkanPompa() {
  if (pompaTerblokir) {
    statusSistem = "POMPA TERBLOKIR! Tidak bisa diaktifkan.";
    return;
  }

  relayAktif = !relayAktif;
  digitalWrite(pinRelay, relayAktif ? HIGH : LOW);
  
  if (relayAktif) {
    volumeAwalPompa = volumeAir;
    statusSistem = "Pompa AKTIF - mengambil air";
    bunyikanTone(500);
  } else {
    int volumeTerambil = volumeAwalPompa - volumeAir;
    statusSistem = "Pompa MATI - air terambil: " + String(volumeTerambil) + "ml";
    bunyikanTone(300); delay(200); bunyikanTone(300);
  }
}

void matikanPompa() {
  if (relayAktif) {
    relayAktif = false;
    digitalWrite(pinRelay, LOW);
    statusSistem = "Pompa dimatikan paksa dari sistem";
    bunyikanTone(200); delay(100); bunyikanTone(200);
  }
}

void blokirPompa(String alasan) {
  matikanPompa(); // Matikan pompa jika sedang aktif
  pompaTerblokir = true;
  alasanBlokir = alasan;
  adaRequestUnblock = false; // Reset request
  statusSistem = "POMPA DIBLOKIR - " + alasan;
  
  // Bunyi peringatan blokir
  for (int i = 0; i < 3; i++) {
    bunyikanTone(1500, 150);
    delay(200);
  }
  
  Serial.println("Pompa diblokir! Alasan: " + alasan);
}

void unblokPompa() {
  pompaTerblokir = false;
  alasanBlokir = "Tidak ada";
  adaRequestUnblock = false;
  statusSistem = "Pompa dibuka blokirnya - Siap digunakan";
  
  // Bunyi konfirmasi unblock
  bunyikanTone(600, 200);
  delay(100);
  bunyikanTone(800, 200);
  delay(100);
  bunyikanTone(1000, 200);
  
  Serial.println("Pompa berhasil di-unblock!");
}

void cekBatasVolume() {
  if (relayAktif && volumeAwalPompa > 0) {
    int volumeTerambil = volumeAwalPompa - volumeAir;
    if (volumeTerambil >= batasVolume) {
      matikanPompa();
      statusSistem = "Batas volume tercapai (" + String(batasVolume) + "ml)";
    }
  }
}

void cekAlarm() {
  if (!airTerdeteksi && !(objekTerdeteksi && jarakTerukur < jarakMin)) {
    if (millis() - waktuAlarm >= 5000) {
      for (int i = 0; i < 3; i++) {
        bunyikanTone(2000, 200);
        if (i < 2) delay(300);
      }
      waktuAlarm = millis();
    }
  }
}

void tanganiWeb() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Smart Well - Sumur Pintar berbasis IoT</title><style>";
  html += "body{font-family:Arial;margin:0;padding:20px;background:#f5f5f5}";
  html += ".container{max-width:900px;margin:0 auto;background:white;padding:25px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.1)}";
  html += "h1{color:#2c3e50;text-align:center;margin-bottom:25px}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin:20px 0}";
  html += ".card{background:#34495e;color:white;padding:20px;border-radius:8px;text-align:center}";
  html += ".nilai{font-size:2em;font-weight:bold;margin:10px 0}";
  html += ".status{padding:15px;border-radius:8px;margin:15px 0;text-align:center;font-weight:bold}";
  html += ".success{background:#27ae60;color:white}";
  html += ".danger{background:#e74c3c;color:white}";
  html += ".warning{background:#f39c12;color:white}";
  html += ".info{background:#3498db;color:white}";
  html += ".blocked{background:#8e44ad;color:white}"; // Warna untuk status blokir
  html += ".request{background:#e67e22;color:white;animation:blink 1s infinite}"; // Animasi untuk request
  html += "@keyframes blink{0%{opacity:1}50%{opacity:0.5}100%{opacity:1}}";
  html += "button{padding:12px 24px;border:none;border-radius:5px;cursor:pointer;margin:5px;font-size:14px;font-weight:bold}";
  html += ".btn-success{background:#27ae60;color:white}";
  html += ".btn-danger{background:#e74c3c;color:white}";
  html += ".btn-primary{background:#3498db;color:white}";
  html += ".btn-warning{background:#f39c12;color:white}";
  html += ".btn-purple{background:#8e44ad;color:white}";
  html += "input{padding:8px;border:1px solid #ddd;border-radius:4px;margin:5px}";
  html += ".kontrol{background:#ecf0f1;padding:20px;border-radius:8px;margin:15px 0}";
  html += ".alert{padding:15px;border-radius:8px;margin:15px 0;font-weight:bold;text-align:center}";
  html += ".alert-request{background:#e67e22;color:white;animation:pulse 2s infinite}";
  html += "@keyframes pulse{0%{transform:scale(1)}50%{transform:scale(1.02)}100%{transform:scale(1)}}";
  html += "</style>";
  
  html += "<script>";
  html += "function updateData(){fetch('/api').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('jarak').textContent=d.jarak.toFixed(2);";
  html += "document.getElementById('volume').textContent=d.volume.toFixed(1);";
  html += "document.getElementById('persen').textContent=d.persentase.toFixed(1);";
  html += "document.getElementById('status').textContent=d.status_sistem;";
  html += "document.getElementById('relay').textContent=d.relay_aktif?'HIDUP':'MATI';";
  html += "document.getElementById('blokir').textContent=d.pompa_terblokir?'TERBLOKIR':'TIDAK TERBLOKIR';";
  html += "document.getElementById('alasan').textContent=d.alasan_blokir;";
  
  html += "let statusDiv=document.getElementById('statusDiv');";
  html += "if(d.pompa_terblokir){";
  html += "statusDiv.className='status blocked';";
  html += "}else if(d.air_terdeteksi){";
  html += "statusDiv.className='status success';";
  html += "}else if(d.jarak<6&&d.objek_terdeteksi){";
  html += "statusDiv.className='status warning';";
  html += "}else{";
  html += "statusDiv.className='status danger';}";
  
  // Update tampilan request unblock
  html += "let requestDiv=document.getElementById('requestAlert');";
  html += "if(d.ada_request_unblock){";
  html += "requestDiv.style.display='block';";
  html += "}else{";
  html += "requestDiv.style.display='none';}";
  
  html += "}).catch(e=>console.log('Error:',e));}";
  
  html += "function kirimKontrol(aksi,nilai=''){";
  html += "let url='/kontrol?aksi='+aksi;";
  html += "if(nilai)url+='&nilai='+nilai;";
  html += "fetch(url).then(r=>r.text()).then(d=>{alert(d);updateData();});}";
  html += "setInterval(updateData,500);window.onload=updateData;</script>";
  
  html += "</head><body><div class='container'>";
  html += "<h1>🌊 Smart Well - Sumur Pintar berbasis IoT</h1>";
  
  html += "<div id='statusDiv' class='status info'>Status: <span id='status'>Loading...</span></div>";
  
  // Alert untuk request unblock
  html += "<div id='requestAlert' class='alert alert-request' style='display:none'>";
  html += "🚨 <strong>PERMINTAAN UNBLOCK!</strong> Seseorang meminta untuk membuka blokir pompa. Klik tombol 'Unblock Pompa' untuk mengizinkan.";
  html += "</div>";
  
  html += "<div class='grid'>";
  html += "<div class='card'><div>📏 Jarak Sensor</div><div class='nilai'><span id='jarak'>--</span> cm</div></div>";
  html += "<div class='card'><div>💧 Volume Air</div><div class='nilai'><span id='volume'>--</span> ml</div></div>";
  html += "<div class='card'><div>📊 Persentase</div><div class='nilai'><span id='persen'>--</span>%</div></div>";
  html += "</div>";
  
  html += "<div class='kontrol'><h3>⚙️ Status Sistem</h3>";
  html += "<p><strong>Mode:</strong> AKTIF dengan Sistem Blokir</p>";
  html += "<p><strong>Relay Pompa:</strong> <span id='relay'>--</span></p>";
  html += "<p><strong>Status Blokir:</strong> <span id='blokir'>--</span></p>";
  html += "<p><strong>Alasan Blokir:</strong> <span id='alasan'>--</span></p></div>";
  
  html += "<div class='kontrol'><h3>🎛️ Kontrol Sistem</h3>";
  html += "<button class='btn-danger' onclick='kirimKontrol(\"matikan_pompa\")'>Matikan Pompa</button>";
  html += "<button class='btn-purple' onclick='kirimKontrol(\"blokir_pompa\",prompt(\"Masukkan alasan blokir:\"))'>Blokir Pompa</button>";
  html += "<button class='btn-success' onclick='kirimKontrol(\"unblock_pompa\")'>Unblock Pompa</button><br>";
  html += "<input type='number' id='batas' placeholder='Batas Volume (ml)' value='" + String(batasVolume) + "'>";
  html += "<button class='btn-warning' onclick='kirimKontrol(\"set_batas\",document.getElementById(\"batas\").value)'>Set Batas</button>";
  html += "</div>";
  
  html += "<div class='kontrol'><h3>📋 Informasi Blokir</h3>";
  html += "<p><strong>Cara Menggunakan:</strong></p>";
  html += "<ul><li>Jika pompa diblokir, pengguna tidak bisa mengaktifkan pompa dengan tombol fisik</li>";
  html += "<li>Pengguna dapat menekan tombol di Pin 13 untuk meminta unblock</li>";
  html += "<li>Monitor akan menerima notifikasi dan dapat mengizinkan atau menolak</li>";
  html += "<li>Hanya monitor yang dapat memblokir/unblock pompa melalui web</li></ul>";
  html += "</div></div></body></html>";
  
  server.send(200, "text/html", html);
}

void tanganiAPI() {
  String statusText = "Standby";
  if (pompaTerblokir) {
    statusText = "TERBLOKIR - " + alasanBlokir;
  } else if (!objekTerdeteksi) {
    statusText = "Sensor Error";
  } else if (!airTerdeteksi) {
    if (jarakTerukur < jarakMin) statusText = "Buffer Zone";
    else statusText = "Tidak Ada Air";
  } else {
    statusText = statusSistem;
  }

  String json = "{\"jarak\":" + String(jarakTerukur) +
                ",\"volume\":" + String(volumeAir) +
                ",\"persentase\":" + String(persentaseAir) +
                ",\"objek_terdeteksi\":" + (objekTerdeteksi?"true":"false") +
                ",\"air_terdeteksi\":" + (airTerdeteksi?"true":"false") +
                ",\"relay_aktif\":" + (relayAktif?"true":"false") +
                ",\"pompa_terblokir\":" + (pompaTerblokir?"true":"false") +
                ",\"ada_request_unblock\":" + (adaRequestUnblock?"true":"false") +
                ",\"alasan_blokir\":\"" + alasanBlokir + "\"" +
                ",\"batas_volume\":" + String(batasVolume) +
                ",\"status_sistem\":\"" + statusText + "\"}";
  
  server.send(200, "application/json", json);
}

void tanganiKontrol() {
  String aksi = server.arg("aksi");
  String nilai = server.arg("nilai");
  String respon = "OK";

  if (aksi == "matikan_pompa") {
    matikanPompa();
    respon = "Pompa dimatikan";
  }
  else if (aksi == "blokir_pompa") {
    String alasan = nilai;
    if (alasan == "" || alasan == "null") {
      alasan = "Diblokir oleh monitor";
    }
    blokirPompa(alasan);
    respon = "Pompa diblokir: " + alasan;
  }
  else if (aksi == "unblock_pompa") {
    unblokPompa();
    respon = "Pompa berhasil di-unblock";
  }
  else if (aksi == "set_batas") {
    batasVolume = nilai.toInt();
    if (batasVolume < 10) batasVolume = 10;
    if (batasVolume > 500) batasVolume = 500;
    respon = "Batas volume diset: " + String(batasVolume) + "ml";
  }

  server.send(200, "text/plain", respon);
}