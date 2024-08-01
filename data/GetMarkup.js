(function() {
    function GetValue(url, callback) {
        fetch(url)
          .then((response) => response.text())
          .then(callback)
          .catch(function(err) {
            console.log('Fetch Error :-S', err);
          });
    }
    
    const imports = document.querySelectorAll("markup");

    for (let index = 0; index < imports.length; index++) {
        const _import = imports[index];
        
        GetValue(_import.textContent, (data) => {
            document.body.innerHTML = document.body.innerHTML
                .replace(_import.outerHTML, data);
        });
    }
})()