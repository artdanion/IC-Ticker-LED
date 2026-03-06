const client = new Paho.MQTT.Client( "mqtt.devlol.org", 443, "/mqtt", "ticker_" + Math.random().toString(16).slice(2) );
client.connect({ useSSL: true, onSuccess: () => console.log("MQTT connected") });

function send() 
{ 
  const text = document.getElementById("msg").value; if (!text || text.length > 30) 
  {
    alert("max 30 characters"); return;
  }
  const message = new Paho.MQTT.Message(text);
  message.destinationName = "devlol/IC-Ticker";
  client.send(message);
}
