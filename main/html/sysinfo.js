
$(() => { // Shorthand for $(document).ready()
  loadData();
  setInterval(loadData, 5000);
});

const readBattery = (mac) => {
  getData("/updatebattery", { mac },
    () => { }, // Empty success callback
    (xhr, status, error) => {
      console.error('Errore nel caricamento dei dati:', status, error);
    }
  );
};

const formatMacAddress = (mac) => {
  return mac.includes(':') ? mac : mac.match(/.{1,2}/g)?.join(':') || '';
};

function updatePage(data) {
  const fields = {
    device: 'gateway',
    firmware: 'firmware',
    uptime: 'uptime',
    ssid: 'ssid',
    //wifi_rssi: 'wifi_rssi',
    macaddr: 'macaddr',
  };

  //<i class="fas fa-check-circle" style="color:green"></i>

  for (const field in fields) {
    let text = data[fields[field]] || ''; // Default to empty string if data is missing
    if (field === 'firmware' && data.build) {
      text += ' - Build: ';
      text += ` ${data.build}`;
    }
    $(`#${field}`).text(text);
  }

  if (data.wifi_rssi) {
    let text = '';
    if (data.wifi_rssi > -50) {
      $('#wifi_rssi').html('<i class="fas fa-signal" style="color:#009900"></i>&nbsp;&nbsp;' + data.wifi_rssi + ' dbm ');
    } else if (data.wifi_rssi > -70) {
      $('#wifi_rssi').html('<i class="fas fa-signal" style="color:#00e600"></i>&nbsp;&nbsp;' + data.wifi_rssi + ' dbm ');
    } else if (data.wifi_rssi > -80) {
      $('#wifi_rssi').html('<i class="fas fa-signal" style="color:#ff9900"></i>&nbsp;&nbsp;' + data.wifi_rssi + ' dbm ');
    } else if (data.wifi_rssi > -90) {
      $('#wifi_rssi').html('<i class="fas fa-signal" style="color:#fb7b50"></i>&nbsp;&nbsp;' + data.wifi_rssi + ' dbm ');
    } else {
      $('#wifi_rssi').html('<i class="fas fa-signal" style="color:#ff0000"></i>&nbsp;&nbsp;' + data.wifi_rssi + ' dbm ');
    }
  } else {
    text('N/A');
  }

  $('#heap-div').toggle(!!data.memory);
  if (data.memory) {
    $('#heap').text(data.memory);
  }

  $('#cpu-div').toggle(!!data.cpu);
  let text = '';
  if (data.cpu) {
    if (data.esp_temp) {
      text += ` ${data.cpu}`;
      text += ' @ ';
      text += ` ${data.esp_temp}`;
    } else {
      text = ` ${data.cpu}`;
    }
    $('#cpu').text(text);
  }

  const $deviceTable = $('#devices-table');
  const hasBattery = !!data.battery;
  $deviceTable.find('th:nth-child(5), td:nth-child(5)').toggle(hasBattery);
  $deviceTable.find('th:nth-child(6), td:nth-child(6)').toggle(hasBattery);

  $("#devicesTableBody").empty();

  if (window.innerWidth < 768) {
    const $devicesCards = $("#devices-cards");
    const $devicesTable = $("#devices-table");

    $devicesTable.hide();
    $devicesCards.show().empty();

    data.devices.forEach(item => {
      const $card = $("<div class='device-card'></div>");
      const $header = $("<div class='device-card-header'></div>").append(`<h3>${item.name || "Unknown Device"}</h3>`);
      const $content = $("<div class='device-card-content'></div>")
        .append(`<div class='card-item'><strong>MAC:</strong> ${formatMacAddress(item.mac)}</div>`)
        .append(`<div class='card-item'><strong>RSSI:</strong> ${item.rssi}</div>`)
        .append(
          item.state === 'Off'
            ? '<div class="card-item"><strong>Status:</strong> <span class="status-away"><i class="fas fa-times-circle" style="color:red"></i> Away</span></div>'
            : '<div class="card-item"><strong>Status:</strong> <span class="status-present"><i class="fas fa-check-circle" style="color:green"></i> Present</span></div>'
        );

      if (data.battery && item.battery) {
        const batteryValue = item.battery < 0 ? "N/A" : `${item.battery}%`;
        $content.append(
          `<div class='card-item'>
            <strong>Battery:</strong>
            <div class='battery-info'>
              <span class='battery-value'>${batteryValue}</span>
              ${item.state === 'On' ? `<button class='btn-small refresh-battery' onclick="readBattery('${item.mac}')"><i class='fas fa-sync-alt'></i></button>` : ''}
            </div>
          </div>`
        );
      }

      if (data.battery && item.bttime && item.battery && item.battery > 0 && item.state === 'On') {
        $content.append(`<div class='card-item'><strong>Last Battery Read:</strong> ${timeConverter(item.bttime)}</div>`);
      }

      $card.append($header, $content);
      $devicesCards.append($card);
    });
  } else {
    const $table = $("#devicesTableBody");

    data.devices.forEach(item => {
      const $row = $("<tr/>");
      $row.append($("<td/>").text(formatMacAddress(item.mac)));
      $row.append($("<td/>").text(item.name || "Unknown"));
      $row.append($("<td/>").text(item.rssi));

      const $statusCell = $("<td/>").html(
        item.state === 'Off'
          ? '<span class="status-away"><i class="fas fa-times-circle" style="color:red"></i> Away</span>'
          : '<span class="status-present"><i class="fas fa-check-circle" style="color:green"></i> Present</span>'
      );
      $row.append($statusCell);

      const $batteryCell = $("<td/>");
      if (data.battery) {
        if (item.battery) {
          const batteryValue = item.battery < 0 ? "N/A" : `${item.battery}%`;
          $batteryCell.html(
            `<div class='battery-info'>
               <span class='battery-value'>${batteryValue}</span>
               ${item.state === 'On' ? `<button class='btn-small refresh-battery' onclick="readBattery('${item.mac}')"><i class='fas fa-sync-alt'></i></button>` : ''}
             </div>`
          );
        } else {
          $batteryCell.html(
            `<div class='battery-info'>
               <span class='battery-value'>N/A</span>
             </div>`
          );
        }
        $row.append($batteryCell);

        const $lastSeenCell = $("<td/>").text(
          item.bttime && item.battery && item.battery > 0 ? timeConverter(item.bttime) : ""
        );
        $row.append($lastSeenCell);
      }

      $table.append($row);
    });
  }
}

const loadData = () => {
  getData(
    "/getsysinfodata",
    null,
    updatePage,
    (xhr, status, error) => {
      console.error('Errore nel caricamento dei dati:', status, error);
    }
  );
};