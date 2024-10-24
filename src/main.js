let socketConnetion=new WebSocket(`ws://${location.hostname}:81/`);
const turnOff=()=>socketConnetion.send('ledoff'),turnOn=()=>socketConnetion.send('ledon');

socketConnetion.onmessage = (e) => { 
    let data = JSON.parse(e.data); 
    updateTag('distance', data.distance)
};

addEventListener
const form = document.getElementById('form');
form.addEventListener('submit', (e) => {
    e.preventDefault();
    const name = document.getElementById('name');
    fetch('https://script.google.com/macros/s/AKfycbzJ96VRdQL4DhxYf_DnHj45tmnpLeJytldWLuftHjyorVCBrvA0gEi44BM9TGi1oLjMpA/exec', {
        method: 'POST',
        headers: {
            'content-Type': 'application/json',
        },
        body: JSON.stringify({
            nombre:name.value,
            distance: name.value
        })
    }).then(response=>response.json())
    .then(data => {
        console.log('respuesta del servidor:', data);
    }).catch(error => {
        console.error('Error', error);
    });

    socketConnetion.send();
    form.reset();
})

const updateTag = (id, value) => {
    const tag = document.getElementById(id); 
    tag.textContent = `altura: ${value}`
}