#!/bin/bash

echo "Sistem paketleri guncelleniyor..."
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

echo "Docker GPG anahtari ekleniyor..."
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --yes --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo "Docker reposu ekleniyor..."
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

echo "Docker kurulumu yapiliyor..."
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

echo "Kullanici Docker grubuna ekleniyor..."
sudo usermod -aG docker $USER

echo "--------------------------------------------------------"
echo "Kurulum Tamamlandi!"
echo "Degisikliklerin aktif olmasi icin terminali KAPATIP YENIDEN ACIN."
echo "Terminali yeniden actiktan sonra servisi baslatmak icin:"
echo "sudo service docker start"
echo "yazmayi unutmayin."
echo "--------------------------------------------------------"
