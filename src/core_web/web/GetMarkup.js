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
            // Вставляем фрагмент на место тега <markup>, НЕ перезаписывая
            // document.body.innerHTML: иначе уничтожаются все элементы и
            // обработчики, навешанные скриптами страницы (кнопки, дерево, сплиттер).
            if (!_import.parentNode) { return; }
            const tpl = document.createElement('template');
            tpl.innerHTML = data;
            _import.replaceWith(tpl.content);
        });
    }


})()