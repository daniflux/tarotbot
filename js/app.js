// js/app.js – TarotBot: Emoji Oracle
// Fixed version - cards will draw properly again

// --- DOM refs ---
const drawButton = document.getElementById("drawButton");
const shuffleButton = document.getElementById("shuffleButton");
const deckSelector = document.getElementById("deckSelector");
const themeLink = document.getElementById("deckTheme");

// --- State ---
let tarotCards = [];
let availableCards = [];
let drawnCards = [];
let currentCard = null;
let isDrawing = false;
let currentDeck = 'emoji'; // will be set by determineInitialDeck()

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

// --- Backgrounds ---
// Stars: used for non-Matrix decks
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
function clearStars() {
  const starsContainer = document.getElementById('stars');
  if (starsContainer) starsContainer.innerHTML = '';
}

// Matrix: canvas code-rain background for the emoji-matrix deck only
let matrix = {
  canvas: null,
  ctx: null,
  columns: [],
  fontSize: 16,
  animationId: null,
  lastW: 0,
  lastH: 0,
};

function createMatrixCanvas() {
  if (matrix.canvas) return; // already exists
  matrix.canvas = document.createElement('canvas');
  matrix.canvas.id = 'matrixCanvas';
  document.body.appendChild(matrix.canvas);
  matrix.ctx = matrix.canvas.getContext('2d');
  resizeMatrixCanvas();
  initMatrixColumns();
  startMatrix();
}

function destroyMatrixCanvas() {
  if (matrix.animationId) cancelAnimationFrame(matrix.animationId);
  matrix.animationId = null;
  if (matrix.canvas && matrix.canvas.parentNode) {
    matrix.canvas.parentNode.removeChild(matrix.canvas);
  }
  matrix.canvas = null;
  matrix.ctx = null;
  matrix.columns = [];
}

function resizeMatrixCanvas() {
  if (!matrix.canvas) return;
  const dpr = window.devicePixelRatio || 1;
  const w = window.innerWidth;
  const h = window.innerHeight;
  matrix.canvas.width = Math.floor(w * dpr);
  matrix.canvas.height = Math.floor(h * dpr);
  matrix.canvas.style.width = w + 'px';
  matrix.canvas.style.height = h + 'px';
  matrix.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  matrix.lastW = w; matrix.lastH = h;
  matrix.fontSize = Math.max(14, Math.min(22, Math.floor(w / 80)));
  matrix.ctx.font = matrix.fontSize + "px monospace";
}

function initMatrixColumns() {
  if (!matrix.canvas) return;
  const cols = Math.ceil(window.innerWidth / matrix.fontSize);
  matrix.columns = new Array(cols).fill(0).map(() => ({
    y: Math.floor(Math.random() * -50), // start above view
    speed: 2 + Math.random() * 3,
  }));
}

function drawMatrixFrame() {
  if (!matrix.ctx) return;
  const ctx = matrix.ctx;
  const w = window.innerWidth;
  const h = window.innerHeight;

  // fade trail
  ctx.fillStyle = "rgba(0, 0, 0, 0.08)";
  ctx.fillRect(0, 0, w, h);

  ctx.fillStyle = "#00ff66";
  ctx.textBaseline = "top";
  ctx.font = matrix.fontSize + "px monospace";

  const chars = "01アイウエオカキクケコﾱﾲﾳﾴﾵﾶﾷﾸﾹﾺﾊﾋﾌﾍﾎ01#$%&*+=-";

  const colCount = matrix.columns.length;
  for (let i = 0; i < colCount; i++) {
    const x = i * matrix.fontSize;
    const col = matrix.columns[i];
    const char = chars.charAt(Math.floor(Math.random() * chars.length));
    ctx.fillText(char, x, col.y * matrix.fontSize);

    col.y += col.speed * 0.08;

    if (col.y * matrix.fontSize > h + 100) {
      col.y = Math.floor(Math.random() * -20);
      col.speed = 2 + Math.random() * 3;
    }
  }

  matrix.animationId = requestAnimationFrame(drawMatrixFrame);
}

function startMatrix() {
  if (!matrix.canvas) return;
  matrix.ctx.fillStyle = "#000";
  matrix.ctx.fillRect(0, 0, window.innerWidth, window.innerHeight);
  drawMatrixFrame();
}

window.addEventListener('resize', () => {
  if (!matrix.canvas) return;
  const w = window.innerWidth, h = window.innerHeight;
  if (w !== matrix.lastW || h !== matrix.lastH) {
    resizeMatrixCanvas();
    initMatrixColumns();
  }
});

// --- UI updates ---
function updateDeckCounter() {
  const counter = document.getElementById('deckCounter');
  counter.textContent = `Cards Remaining: ${availableCards.length}/${tarotCards.length}`;
}

// Force text/monochrome emoji where possible (U+FE0E text presentation)
function toTextPresentation(emoji) {
  return (emoji || '') + '\uFE0E';
}

function updateDrawnCardsList() {
  const drawnList = document.getElementById('drawnList');
  if (drawnCards.length === 0) {
    drawnList.innerHTML = '<p style="opacity: 0.7; font-style: italic;">No cards drawn yet</p>';
    return;
  }
  drawnList.innerHTML = drawnCards.map(card => `
    <div class="drawn-card-item">
      <div class="drawn-card-symbol">${toTextPresentation(card.symbol || '')}</div>
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
    cardFront.innerHTML =
      '<img src="' + card.image + '" alt="' + card.name + '" style="width: 100%; border-radius: 12px;">' +
      '<div class="card-name">' + card.name + '</div>';
  } else {
    cardFront.innerHTML =
      '<div class="card-symbol">' + toTextPresentation(card.symbol || '') + '</div>' +
      '<div class="card-name">' + card.name + '</div>' +
      '<div class="card-meaning">' + (card.meaning || '') + '</div>';
  }
}

// --- Actions ---
function shuffleDeck() {
  if (isDrawing) return;
  availableCards = [...tarotCards];
  drawnCards = [];
  currentCard = null;
  // Always re-enable deck selector after shuffle, even if a card was awaiting reveal
  deckSelector.disabled = false;
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
  tarotCardElement.onclick = null;
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

// --- Deck selection helpers ---
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

// Prefer the deck with progress; otherwise random. Works with or without Matrix option present.
function determineInitialDeck() {
  const decks = Array.from(deckSelector.options).map(option => option.value);
  let maxProgress = -1;
  let selectedDeck = decks[0] || 'emoji';

  decks.forEach(deck => {
    const progress = getDeckProgress(deck);
    if (progress > maxProgress) {
      maxProgress = progress;
      selectedDeck = deck;
    }
  });

  if (maxProgress === 0 && decks.length > 0) {
    selectedDeck = decks[Math.floor(Math.random() * decks.length)];
  }

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

// Load deck data + toggle background skin
function loadDeck(deckName) {
  currentDeck = deckName;

  // --- Reset all card state and UI before loading new deck ---
  currentCard = null;
  availableCards = [];
  drawnCards = [];
  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  if (tarotCardWrapper) tarotCardWrapper.classList.remove("awaiting-reveal");
  if (tarotCardElement) tarotCardElement.classList.remove("flipped");
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

  // Toggle backgrounds
  if (deckName === 'emoji-matrix') {
    // Matrix deck: show code rain, hide/clear stars (CSS also hides stars for this deck)
    createMatrixCanvas();
    clearStars();
  } else {
    // Other decks: destroy matrix canvas, show stars
    destroyMatrixCanvas();
    createStars();
  }

  // Theme CSS swap
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

// --- Event listeners ---
drawButton.addEventListener("click", drawCard);
shuffleButton.addEventListener("click", shuffleDeck);

deckSelector.addEventListener("change", function(e) {
  // Save state of current deck before switching
  saveStateToLocalStorage();
  
  // Reset all card-related UI and state
  currentCard = null;
  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  if (tarotCardWrapper) tarotCardWrapper.classList.remove("awaiting-reveal");
  // Reset card-back to only show the moon pattern, remove any reveal text
  const cardBack = tarotCardElement ? tarotCardElement.querySelector(".card-back") : null;
  if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
  document.getElementById("cardFront").innerHTML = "";
  document.getElementById("interpretation").classList.remove("show");
  document.getElementById("interpretationText").textContent = "";
  drawButton.textContent = "Draw Your Card";
  drawButton.disabled = false;
  // Remove any leftover click handler from the card
  if (tarotCardElement) tarotCardElement.onclick = null;

  // Now load the new deck
  loadDeck(e.target.value);
});

// --- Boot ---
setLoadingState(true);
loadDeck(determineInitialDeck());
