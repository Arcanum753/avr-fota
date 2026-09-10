// WiFi slot selection functionality
(function() {
    // Глобальная переменная для хранения текущего выбранного слота
    let currentSelectedSlot = 0;
    
    // Функция выделения слота
    window.selectWifiSlot = function(slotNumber) {
        console.log('Selecting slot:', slotNumber);
        
        // Убираем выделение со всех слотов
        document.querySelectorAll('.config-slot').forEach(slot => {
            slot.classList.remove('selected');
        });
        
        // Выделяем выбранный слот
        const selectedSlot = document.getElementById(`slot${slotNumber}`);
        if (selectedSlot) {
            selectedSlot.classList.add('selected');
            currentSelectedSlot = slotNumber;
            
            // Обновляем отображение
            const displayElement = document.getElementById('selectedSlotDisplay');
            if (displayElement) {
                displayElement.innerText = slotNumber + 1;
            }
            
            // Сохраняем в localStorage последний выбранный слот
            localStorage.setItem('lastSelectedWifiSlot', slotNumber);
            console.log('Slot selected:', slotNumber);
        }
    };
    
    // Функция заполнения SSID в выделенный слот
    window.selssid = function(value) {
        console.log('Filling SSID in slot:', currentSelectedSlot, 'value:', value);
        
        const ssidField = document.getElementById(`p${currentSelectedSlot}_ssid`);
        if (ssidField) {
            ssidField.value = value;
            
            // Визуальный фидбек
            ssidField.style.backgroundColor = '#ffffcc';
            setTimeout(() => {
                ssidField.style.backgroundColor = '';
            }, 500);
            
            console.log('SSID filled successfully');
        }
    };
    
    // Функция инициализации
    function initSlotSelection() {
        console.log('Initializing slot selection...');
        
        // Добавляем обработчики кликов на слоты
        document.querySelectorAll('.config-slot').forEach(slot => {
            slot.addEventListener('click', function(event) {
                // Проверяем, был ли клик по полю ввода или кнопке
                if (event.target.tagName === 'INPUT' || 
                    event.target.tagName === 'BUTTON' || 
                    event.target.tagName === 'A' ||
                    (event.target.type && (event.target.type === 'submit' || 
                                          event.target.type === 'checkbox' || 
                                          event.target.type === 'text' || 
                                          event.target.type === 'password'))) {
                    return; // Не выделяем слот при клике на интерактивные элементы
                }
                
                const slotNum = parseInt(this.dataset.slot);
                window.selectWifiSlot(slotNum);
            });
        });
        
        // Восстанавливаем последний выбранный слот
        const lastSlot = localStorage.getItem('lastSelectedWifiSlot');
        if (lastSlot !== null && lastSlot >= 0 && lastSlot <= 3) {
            window.selectWifiSlot(parseInt(lastSlot));
        } else {
            window.selectWifiSlot(0); // По умолчанию первый слот
        }
    }
    
    // Ждем загрузки DOM и всех подгружаемых компонентов
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initSlotSelection);
    } else {
        // Если DOM уже загружен, но возможно подгружаются markup
        setTimeout(initSlotSelection, 500);
    }
    
    // Также запускаем после загрузки всех ресурсов
    window.addEventListener('load', function() {
        // Даем время на подгрузку markup
        setTimeout(initSlotSelection, 1000);
    });
})();