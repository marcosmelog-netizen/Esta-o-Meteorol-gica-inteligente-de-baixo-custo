<!DOCTYPE html>
<html>
<head>
    <title>Estação Meteorológica IoT</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: Arial; background: #f0f0f0; padding: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: white; border-radius: 10px; padding: 20px; margin: 15px 0; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .sensor-value { font-size: 2em; font-weight: bold; color: #2196F3; }
        .sensor-label { color: #666; margin-bottom: 5px; }
        .rain-alert { background: #FF5722; color: white; padding: 15px; border-radius: 5px; margin: 10px 0; }
        .controls button { background: #4CAF50; color: white; border: none; padding: 10px 20px; margin: 5px; border-radius: 5px; cursor: pointer; }
        .controls button:hover { background: #45a049; }
        .status { padding: 10px; margin: 5px 0; border-radius: 5px; }
        .connected { background: #4CAF50; color: white; }
        .disconnected { background: #f44336; color: white; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌤️ Estação Meteorológica IoT</h1>
        
        <div class="card">
            <div id="status" class="status disconnected">Desconectado</div>
            <div class="controls">
                <button onclick="connectMQTT()">Conectar</button>
                <button onclick="disconnectMQTT()">Desconectar</button>
                <button onclick="sendCommand('LEITURA_AGORA')">Ler Agora</button>
                <button onclick="sendCommand('ABRIR_COBERTURA')">Abrir Cobertura</button>
                <button onclick="sendCommand('FECHAR_COBERTURA')">Fechar Cobertura</button>
            </div>
        </div>

        <div class="grid">
            <div class="card">
                <div class="sensor-label">🌡️ Temperatura</div>
                <div id="temp" class="sensor-value">-- °C</div>
            </div>
            
            <div class="card">
                <div class="sensor-label">💧 Umidade</div>
                <div id="hum" class="sensor-value">-- %</div>
            </div>
            
            <div class="card">
                <div class="sensor-label">☀️ Luminosidade</div>
                <div id="lux" class="sensor-value">-- lux</div>
            </div>
            
            <div class="card">
                <div class="sensor-label">📏 Distância/Chuva</div>
                <div id="dist" class="sensor-value">-- mm</div>
            </div>
        </div>

        <div class="card">
            <h3>📊 Dados Recebidos</h3>
            <pre id="data-log" style="background: #f5f5f5; padding: 10px; border-radius: 5px; max-height: 200px; overflow-y: auto;"></pre>
        </div>

        <div id="rain-alert" class="rain-alert" style="display: none;">
            ⚠️ ALERTA: CHUVA DETECTADA! Cobertura ativada.
        </div>
    </div>

    <script>
        // Configurações MQTT (WebSocket para HiveMQ)
        const broker = 'wss://broker.hivemq.com:8884/mqtt';
        const clientId = 'webapp_' + Math.random().toString(16).substr(2, 8);
        let client = null;
        
        // Tópicos
        const topics = {
            temperatura: 'estacao/temperatura',
            umidade: 'estacao/umidade',
            luminosidade: 'estacao/luminosidade',
            chuva: 'estacao/chuva',
            status: 'estacao/status',
            comandos: 'estacao/comandos'
        };
        
        // Conectar ao broker
        function connectMQTT() {
            client = new Paho.MQTT.Client(broker, clientId);
            
            client.onConnectionLost = onConnectionLost;
            client.onMessageArrived = onMessageArrived;
            
            client.connect({
                onSuccess: onConnect,
                onFailure: onFailure,
                useSSL: true,
                reconnect: true
            });
        }
        
        function onConnect() {
            document.getElementById('status').className = 'status connected';
            document.getElementById('status').textContent = 'Conectado ao broker';
            
            // Inscrever nos tópicos
            for (let topic in topics) {
                client.subscribe(topics[topic], {qos: 1});
                console.log('Inscrito em:', topics[topic]);
            }
        }
        
        function onFailure(error) {
            console.error('Falha conexão:', error);
            document.getElementById('status').className = 'status disconnected';
            document.getElementById('status').textContent = 'Erro: ' + error.errorMessage;
        }
        
        function onConnectionLost(response) {
            if (response.errorCode !== 0) {
                console.log('Conexão perdida:', response.errorMessage);
                document.getElementById('status').className = 'status disconnected';
                document.getElementById('status').textContent = 'Desconectado';
            }
        }
        
        function onMessageArrived(message) {
            console.log('Mensagem:', message.destinationName, message.payloadString);
            
            const log = document.getElementById('data-log');
            log.textContent = new Date().toLocaleTimeString() + ' - ' + 
                             message.destinationName + ': ' + 
                             message.payloadString + '\n' + log.textContent;
            
            try {
                const data = JSON.parse(message.payloadString);
                
                // Atualizar interface
                switch(message.destinationName) {
                    case topics.temperatura:
                        document.getElementById('temp').textContent = data.temp + ' °C';
                        break;
                    case topics.umidade:
                        document.getElementById('hum').textContent = data.umid + ' %';
                        break;
                    case topics.luminosidade:
                        document.getElementById('lux').textContent = data.lux + ' lux';
                        break;
                    case topics.chuva:
                        document.getElementById('dist').textContent = data.dist + ' mm';
                        if (data.chuva) {
                            document.getElementById('rain-alert').style.display = 'block';
                        } else {
                            document.getElementById('rain-alert').style.display = 'none';
                        }
                        break;
                    case topics.status:
                        console.log('Status:', data);
                        break;
                }
            } catch (e) {
                console.error('Erro parse JSON:', e);
            }
        }
        
        function disconnectMQTT() {
            if (client && client.isConnected()) {
                client.disconnect();
                document.getElementById('status').className = 'status disconnected';
                document.getElementById('status').textContent = 'Desconectado';
            }
        }
        
        function sendCommand(command) {
            if (client && client.isConnected()) {
                const message = new Paho.MQTT.Message(command);
                message.destinationName = topics.comandos;
                message.qos = 1;
                client.send(message);
                console.log('Comando enviado:', command);
            } else {
                alert('Conecte-se primeiro ao broker!');
            }
        }
        
        // Inicializar MQTT Library
        const script = document.createElement('script');
        script.src = 'https://cdnjs.cloudflare.com/ajax/libs/paho-mqtt/1.1.0/paho-mqtt.min.js';
        script.onload = function() {
            console.log('MQTT library carregada');
            connectMQTT(); // Conectar automaticamente
        };
        document.head.appendChild(script);
    </script>
</body>
</html> 