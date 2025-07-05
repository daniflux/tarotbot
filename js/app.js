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

// --- LocalStorage helpers for deck state ---
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
    if (card.image) {
        cardFront.innerHTML = '<img src="' + card.image + '" alt="' + card.name + '" style="width: 100%; border-radius: 12px;"><div class="card-name">' + card.name + '</div>';
    } else {
        cardFront.innerHTML = '<div class="card-symbol">' + (card.symbol || '') + '</div><div class="card-name">' + card.name + '</div><div class="card-meaning">' + (card.meaning || '') + '</div>';
    }
}

function shuffleDeck() {
    if (isDrawing) return;
    availableCards = [...tarotCards];
    drawnCards = [];
    currentCard = null; // Ensure currentCard is cleared to prevent deck-switcher lockout after shuffling an unrevealed card
    localStorage.removeItem('tarotState_' + currentDeck);
    updateDeckCounter();
    updateDrawnCardsList();
    const tarotCardElement = document.getElementById("tarotCard");
    const tarotCardWrapper = document.getElementById("tarotCardWrapper");
    tarotCardElement.classList.remove("flipped");
    if (tarotCardWrapper) tarotCardWrapper.classList.remove("awaiting-reveal");
    // Reset card-back to only show the moon pattern, remove any reveal text
    const cardBack = tarotCardElement.querySelector(".card-back");
    if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    document.getElementById("cardFront").innerHTML = "";
    document.getElementById("interpretation").classList.remove("show");
    document.getElementById("interpretationText").textContent = "";
    drawButton.textContent = "Draw Your Card";
    drawButton.disabled = false;
    // Remove any leftover click handler from the card
    document.getElementById("tarotCard").onclick = null;
    saveStateToLocalStorage();
}

function drawCard() {
    if (availableCards.length === 0 || isDrawing) return;
    isDrawing = true;
    const tarotCardElement = document.getElementById("tarotCard");
    const tarotCardWrapper = document.getElementById("tarotCardWrapper");
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
        // Only show reveal text and sparkle if a card is drawn
        cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
        if (currentCard) {
            const revealTextDiv = document.createElement('div');
            revealTextDiv.className = 'reveal-text';
            revealTextDiv.textContent = 'Click to reveal';
            cardBack.appendChild(revealTextDiv);
            tarotCardWrapper.classList.add("awaiting-reveal");
            // Only make the card clickable if a card is in play
            tarotCardElement.onclick = function () {
                if (this.classList.contains("flipped")) return;
                tarotCardWrapper.classList.remove("awaiting-reveal");
                this.classList.add("flipped");
                // Remove from available, add to drawn, update state
                const idx = availableCards.findIndex(c => c.name === currentCard.name);
                if (idx > -1) availableCards.splice(idx, 1);
                drawnCards.unshift(currentCard);
                updateDeckCounter();
                updateDrawnCardsList();
                saveStateToLocalStorage();
                // Re-enable deck selector after reveal
                deckSelector.disabled = false;
                setTimeout(() => {
                    interpretationTextElement.textContent = currentCard.interpretation;
                    interpretationElement.classList.add("show");
                }, 400);
            };
        } else {
            tarotCardWrapper.classList.remove("awaiting-reveal");
            tarotCardElement.onclick = null;
        }
        // Disable deck selector while a card is awaiting reveal
        deckSelector.disabled = true;
        drawButton.textContent = availableCards.length > 0 ? "Draw Next Card" : "Deck Empty";
        drawButton.disabled = availableCards.length === 0;
        isDrawing = false;
        saveStateToLocalStorage();
    }, wasFlipped ? 800 : 0);
}

function getDeckProgress(deckName) {
    const state = localStorage.getItem(`tarotState_${deckName}`);
    if (!state) return 0;
    try {
        const parsed = JSON.parse(state);
        return parsed.drawnCardNames?.length || 0;
    } catch (e) {
        return 0;
    }
}

function determineInitialDeck() {
    const decks = Array.from(deckSelector.options).map(option => option.value);
    let maxProgress = -1;
    let selectedDeck = null;

    // Check progress for each deck
    decks.forEach(deck => {
        const progress = getDeckProgress(deck);
        if (progress > maxProgress) {
            maxProgress = progress;
            selectedDeck = deck;
        }
    });

    // If no progress found for any deck, pick a random one
    if (maxProgress === 0) {
        selectedDeck = decks[Math.floor(Math.random() * decks.length)];
    }

    // Update the dropdown selection
    deckSelector.value = selectedDeck;
    return selectedDeck;
}

function setLoadingState(isLoading) {
    drawButton.disabled = isLoading;
    shuffleButton.disabled = isLoading;
    if (isLoading) {
        document.getElementById("deckCounter").textContent = "Loading deck...";
        document.getElementById("drawnList").innerHTML = '<p style="opacity: 0.7; font-style: italic;">No cards drawn yet</p>';
        document.getElementById("cardFront").innerHTML = "";
        document.getElementById("interpretation").classList.remove("show");
        document.getElementById("interpretationText").textContent = "";
    }
}

function loadDeck(deckName) {
    currentDeck = deckName;
    // --- Reset all card state and UI before loading new deck ---
    currentCard = null;
    availableCards = [];
    drawnCards = [];
    // Remove reveal/sparkle UI
    const tarotCardElement = document.getElementById("tarotCard");
    const tarotCardWrapper = document.getElementById("tarotCardWrapper");
    if (tarotCardWrapper) tarotCardWrapper.classList.remove("awaiting-reveal");
    if (tarotCardElement) tarotCardElement.classList.remove("flipped");
    // Clear card front and back
    const cardBack = tarotCardElement ? tarotCardElement.querySelector(".card-back") : null;
    if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    const cardFront = document.getElementById("cardFront");
    if (cardFront) cardFront.innerHTML = "";
    document.getElementById("interpretation").classList.remove("show");
    document.getElementById("interpretationText").textContent = "";
    drawButton.textContent = "Draw Your Card";
    drawButton.disabled = false;
    updateDeckCounter();
    updateDrawnCardsList();
    // --- End reset ---
    themeLink.href = 'decks/' + deckName + '/style.css';
    setLoadingState(true);
    // Remove any previous deckData and deck script
    try { delete window.deckData; } catch (e) { window.deckData = undefined; }
    var oldScripts = document.querySelectorAll('script[data-deck]');
    oldScripts.forEach(function(s) { s.remove(); });
    var script = document.createElement('script');
    script.src = 'decks/' + deckName + '/deck.js';
    script.dataset.deck = deckName;
    script.onload = function() {
        if (!window.deckData || !window.deckData.cards) {
            alert('Failed to load deck data.');
            setLoadingState(true);
            return;
        }
        tarotCards = window.deckData.cards;
        // Try to load state for this deck, or shuffle if none
        if (localStorage.getItem('tarotState_' + currentDeck)) {
            loadStateFromLocalStorage();
        } else {
            shuffleDeck();
        }
        setLoadingState(false);
    };
    script.onerror = function() {
        alert('Failed to load deck script. Please try again or check your files.');
        setLoadingState(true);
    };
    document.body.appendChild(script);
}

drawButton.addEventListener("click", drawCard);
shuffleButton.addEventListener("click", shuffleDeck);
deckSelector.addEventListener("change", function(e) {
    // If a card is drawn but not revealed (not flipped), prompt the user
    const tarotCardElement = document.getElementById("tarotCard");
    const isAwaitingReveal = tarotCardElement && tarotCardElement.onclick && currentCard && !tarotCardElement.classList.contains("flipped");
    if (isAwaitingReveal) {
        const keep = confirm("You have a drawn card that hasn't been revealed yet. Do you want to keep it or discard it?\n\nOK = Keep, Cancel = Discard");
        if (keep) {
            // Move currentCard to drawnCards, remove from availableCards
            const idx = availableCards.findIndex(c => c.name === currentCard.name);
            if (idx > -1) availableCards.splice(idx, 1);
            drawnCards.unshift(currentCard);
        } else {
            currentCard = null;
        }
    saveStateToLocalStorage();
    // Always re-enable deck selector after shuffle
    deckSelector.disabled = false;
    } else {
        // Save state of current deck before switching
        saveStateToLocalStorage();
    }
    // Reset all card-related UI and state
    currentCard = null;
    const tarotCardWrapper = document.getElementById("tarotCardWrapper");
    if (tarotCardWrapper) tarotCardWrapper.classList.remove("awaiting-reveal");
    // Reset card-back to only show the moon pattern, remove any reveal text
    const cardBack = tarotCardElement.querySelector(".card-back");
    if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    document.getElementById("cardFront").innerHTML = "";
    document.getElementById("interpretation").classList.remove("show");
    document.getElementById("interpretationText").textContent = "";
    drawButton.textContent = "Draw Your Card";
    drawButton.disabled = false;
    // Now load the new deck
    loadDeck(e.target.value);
});

createStars();
setLoadingState(true);
loadDeck(determineInitialDeck());
