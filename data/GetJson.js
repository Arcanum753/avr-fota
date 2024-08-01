function GetJson(url, callback) {
  fetch(url)
    .then((response) => response.json())
    .then(callback)
    .catch(function (err) {
      console.log('Fetch Error :-S', err);
    });
}

function GetText(url, callback) {
  fetch(url)
    .then((response) => response.text())
    .then(callback)
    .catch(function (err) {
      console.log('Fetch Error :-S', err);
    });
}

function ApplyJson(url) {
  GetJson(url, (data) => {
    const keys = Object.keys(data);

    for (key of keys) {
      const components = document.querySelectorAll("#" + key);

      for (comp of components) {
        comp.textContent = comp.value = data[key];
      }
    }
  });
}

function ParseCVT(data) {
  const _fields = data.split("\n");
  const result = [];

  for (row of _fields) {
    const fields = row.split("|");

    if (!fields[0]) continue;

    result.push(fields);
  }

  return result;
}

function ApplyCVT(url, prefix = "") {
  return new Promise((resolve, reject) => {
    try {
      GetText(url, (data) => {
        const fieldsRows = ParseCVT(data);

        for (fields of fieldsRows) {
          const components = document.querySelectorAll("#" + prefix + fields[0]);

          for (comp of components) {
            if (fields[2] == "input") {
              comp.value = fields[1];
            } else if (fields[2] == "div") {
              comp.innerHTML = fields[1];

            } else if (fields[2] == "chk") {
              comp.checked = fields[1];
            }
          }
        }

        resolve(data);
      });
    }
    catch (e) {
      reject(e);
    }
  });
}