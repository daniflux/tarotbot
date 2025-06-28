
const drawButton = document.getElementById("drawButton");
const shuffleButton = document.getElementById("shuffleButton");
const deckSelector = document.getElementById("deckSelector");
const themeLink = document.getElementById("deckTheme");

let tarotCards = [];
let availableCards = [];
let drawnCards = [];
let currentCard = null;
let isDrawing = false;
let currentDeck = 'emoji';

function createStars() {
    const starsContainer = document.getElementById('stars');
    if (!starsContainer) return;
    starsContainer.innerHTML = '';
    for (let i = 0; i < 50; i++) {
        const star = document.createElement('div');
        star.className = 'star';
        star.innerHTML = '✦';
        star.style.left = Math.random() * 100 + '%';
        star.style.top = Math.random() * 100 + '%';
        star.style.animationDelay = Math.random() * 3 + 's';
        star.style.fontSize = (Math.random() * 0.5 + 0.5) + 'rem';
        starsContainer.appendChild(star);
    }
}

function updateDeckCounter() {
    const counter = document.getElementById('deckCounter');
    counter.textContent = `Cards Remaining: ${availableCards.length}/${tarotCards.length}`;
}

function updateDrawnCardsList() {
    const drawnList = document.getElementById('drawnList');
    if (drawnCards.length === 0) {
        drawnList.innerHTML = '<p style="opacity: 0.7; font-style: italic;">No cards drawn yet</p>';
        return;
    }
    drawnList.innerHTML = drawnCards.map(card => `
        <div class="drawn-card-item">
            <div class="drawn-card-symbol">${card.symbol || ''}</div>
            <div class="drawn-card-info">
                <div class="drawn-card-name">${card.name}</div>
                <div class="drawn-card-meaning">${card.meaning}</div>
            </div>
        </div>
    `).join('');
}

function displayCard(card) {
    const cardFront = document.getElementById("cardFront");
    cardFront.innerHTML = card.image
        ? \`<img src="\${card.image}" alt="\${card.name}" style="width: 100%; border-radius: 12px;"><div class="card-name">\${card.name}</div>\`
        : \`<div class="card-symbol">\${card.symbol}</div><div class="card-name">\${card.name}</div><div class="card-meaning">\${card.meaning}</div>\`;
}

function shuffleDeck() {
    if (isDrawing) return;
    availableCards = [...tarotCards];
    drawnCards = [];
    currentCard = null;
    localStorage.removeItem('tarotState_' + currentDeck);
    updateDeckCounter();
    updateDrawnCardsList();
    document.getElementById("tarotCard").classList.remove("flipped");
    document.getElementById("cardFront").innerHTML = "";
    document.getElementById("interpretation").classList.remove("show");
    document.getElementById("interpretationText").textContent = "";
    drawButton.textContent = "Draw Your Card";
    drawButton.disabled = false;
}

function drawCard() {
    if (availableCards.length === 0 || isDrawing) return;
    isDrawing = true;
    const tarotCardElement = document.getElementById("tarotCard");
    const cardBack = tarotCardElement.querySelector(".card-back");
    const interpretationElement = document.getElementById("interpretation");
    const interpretationTextElement = document.getElementById("interpretationText");
    const wasFlipped = tarotCardElement.classList.contains("flipped");
    tarotCardElement.classList.remove("flipped");
    drawButton.disabled = true;
    setTimeout(() => {
        const randomIndex = Math.floor(Math.random() * availableCards.length);
        currentCard = availableCards[randomIndex];
        displayCard(currentCard);
        const revealTextDiv = document.createElement('div');
        revealTextDiv.className = 'reveal-text';
        revealTextDiv.textContent = 'Click to reveal';
        cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
        cardBack.appendChild(revealTextDiv);
        tarotCardElement.onclick = function () {
            if (this.classList.contains("flipped")) return;
            this.classList.add("flipped");
            availableCards.splice(randomIndex, 1);
            drawnCards.unshift(currentCard);
            updateDeckCounter();
            updateDrawnCardsList();
            saveStateToLocalStorage();
            setTimeout(() => {
                interpretationTextElement.textContent = currentCard.interpretation;
                interpretationElement.classList.add("show");
            }, 400);
        };
        drawButton.textContent = availableCards.length > 0 ? "Draw Next Card" : "Deck Empty";
        drawButton.disabled = availableCards.length === 0;
        isDrawing = false;
    }, wasFlipped ? 800 : 0);
}

function saveStateToLocalStorage() {
    const state = {
        availableCardNames: availableCards.map(c => c.name),
        drawnCardNames: drawnCards.map(c => c.name),
        currentCardName: currentCard ? currentCard.name : null
    };
    localStorage.setItem("tarotState_" + currentDeck, JSON.stringify(state));
}

function loadStateFromLocalStorage() {
    const saved = localStorage.getItem("tarotState_" + currentDeck);
    if (!saved) return;
    const state = JSON.parse(saved);
    availableCards = tarotCards.filter(c => state.availableCardNames.includes(c.name));
    drawnCards = state.drawnCardNames.map(name => tarotCards.find(c => c.name === name)).filter(Boolean);
    currentCard = tarotCards.find(c => c.name === state.currentCardName);
    if (currentCard) displayCard(currentCard);
    updateDeckCounter();
    updateDrawnCardsList();
}

function loadDeck(deckName) {
    currentDeck = deckName;
    themeLink.href = \`decks/\${deckName}/style.css\`;
    document.querySelectorAll("script[data-deck]").forEach(s => s.remove());
    const script = document.createElement("script");
    script.src = \`decks/\${deckName}/deck.js\`;
    script.dataset.deck = deckName;
    script.onload = () => {
        tarotCards = window.deckData.cards;
        shuffleDeck();
        loadStateFromLocalStorage();
    };
    document.body.appendChild(script);
}

drawButton.addEventListener("click", drawCard);
shuffleButton.addEventListener("click", shuffleDeck);
deckSelector.addEventListener("change", e => loadDeck(e.target.value));

createStars();
loadDeck("emoji");
