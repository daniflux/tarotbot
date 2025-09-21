// js/app.js — TarotBot: Emoji Oracle (fixed reveal + Matrix deck option)

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

// --- Reveal click handling (robust) ---
let readyToReveal = false;
let revealHandlerBound = false;
function onCardRevealClick() {
  if (!readyToReveal) return;
  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  const interpretationElement = document.getElementById("interpretation");
  const interpretationTextElement = document.getElementById("interpretationText");

  if (!tarotCardElement || tarotCardElement.classList.contains("flipped")) return;

  readyToReveal = false; // prevent double triggers
  tarotCardWrapper?.classList.remove("awaiting-reveal");
  tarotCardElement.classList.add("flipped");

  // Move current card from available -> drawn
  const idx = availableCards.findIndex(c => c.name === (currentCard && currentCard.name));
  if (idx > -1) availableCards.splice(idx, 1);
  if (currentCard) drawnCards.unshift(currentCard);

  updateDeckCounter();
  updateDrawnCardsList();
  saveStateToLocalStorage();

  // Re-enable deck switch now that reveal happened
  deckSelector.disabled = false;

  setTimeout(() => {
    if (currentCard) {
      interpretationTextElement.textContent = currentCard.interpretation;
      interpretationElement.classList.add("show");
    }
  }, 400);
}

function bindRevealHandler() {
  const tarotCardElement = document.getElementById("tarotCard");
  if (!tarotCardElement || revealHandlerBound) return;
  tarotCardElement.addEventListener("click", onCardRevealClick);
  revealHandlerBound = true;
}
function unbindRevealHandler() {
  const tarotCardElement = document.getElementById("tarotCard");
  if (!tarotCardElement || !revealHandlerBound) return;
  tarotCardElement.removeEventListener("click", onCardRevealClick);
  revealHandlerBound = false;
}

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
// Stars for non-Matrix decks
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

// Matrix code rain for emoji-matrix only
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
  // ensure canvas sits behind the card
  matrix.canvas.style.position = 'fixed';
  matrix.canvas.style.inset = '0';
  matrix.canvas.style.zIndex = '-1';
  matrix.canvas.style.background = '#000';
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
    speed: 1 + Math.random() * 2,
  }));
}

function drawMatrixFrame() {
  if (!matrix.ctx) return;
  const ctx = matrix.ctx;
  const w = window.innerWidth;
  const h = window.innerHeight;

  // trailing fade
  ctx.fillStyle = "rgba(0, 0, 0, 0.015)";
  ctx.fillRect(0, 0, w, h);

  ctx.fillStyle = "#00ff66";
  ctx.textBaseline = "top";
  ctx.font = matrix.fontSize + "px monospace";

  const chars = "01アイウエオカキクケコｱｲｳｴｵｶｷｸｹｺﾊﾋﾌﾍﾎ01#$%&*+=-";

  const colCount = matrix.columns.length;
  for (let i = 0; i < colCount; i++) {
    const x = i * matrix.fontSize;
    const col = matrix.columns[i];
    const char = chars.charAt(Math.floor(Math.random() * chars.length));
    ctx.fillText(char, x, col.y * matrix.fontSize);

    col.y += col.speed * 0.04;

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

// Prefer a text/monochrome emoji rendering (falls back gracefully)
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
  currentCard = null; // ensure no pending reveal
  deckSelector.disabled = false;
  localStorage.removeItem('tarotState_' + currentDeck);
  updateDeckCounter();
  updateDrawnCardsList();

  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  tarotCardElement.classList.remove("flipped");
  tarotCardWrapper?.classList.remove("awaiting-reveal");

  // Reset card faces & interpretation
  const cardBack = tarotCardElement.querySelector(".card-back");
  if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
  document.getElementById("cardFront").innerHTML = "";
  document.getElementById("interpretation").classList.remove("show");
  document.getElementById("interpretationText").textContent = "";

  drawButton.textContent = "Draw Your Card";
  drawButton.disabled = false;

  // Clear click readiness
  readyToReveal = false;
  unbindRevealHandler();

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

  // Make sure the card is definitely on top/clickable
  tarotCardElement.style.position = 'relative';
  tarotCardElement.style.zIndex = '1';
  tarotCardElement.style.cursor = 'pointer';

  const wasFlipped = tarotCardElement.classList.contains("flipped");
  tarotCardElement.classList.remove("flipped");
  drawButton.disabled = true;

  const prep = () => {
    // choose a random card
    const randomIndex = Math.floor(Math.random() * availableCards.length);
    currentCard = availableCards[randomIndex];

    // populate the front
    displayCard(currentCard);

    // reset back, add "Click to reveal"
    cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    if (currentCard) {
      const revealTextDiv = document.createElement('div');
      revealTextDiv.className = 'reveal-text';
      revealTextDiv.textContent = 'Click to reveal';
      cardBack.appendChild(revealTextDiv);
      tarotCardWrapper.classList.add("awaiting-reveal");
    } else {
      tarotCardWrapper.classList.remove("awaiting-reveal");
    }

    // arm click AFTER card is ready
    readyToReveal = !!currentCard;
    unbindRevealHandler();
    bindRevealHandler();

    // hide interpretation until reveal
    interpretationElement.classList.remove("show");
    interpretationTextElement.textContent = "";

    // while awaiting reveal, lock deck switch to avoid weird state
    deckSelector.disabled = true;

    // button state
    drawButton.textContent = availableCards.length > 0 ? "Draw Next Card" : "Deck Empty";
    drawButton.disabled = availableCards.length === 0;

    isDrawing = false;
    saveStateToLocalStorage();
  };

  // if it was previously flipped, give it a tiny beat to reset
  if (wasFlipped) {
    setTimeout(prep, 250);
  } else {
    prep();
  }
}

// --- Deck selection helpers ---
function getDeckProgress(deckName) {
  const state = localStorage.getItem(`tarotState_${deckName}`);
  if (!state) return 0;
  try {
    const parsed = JSON.parse(state);
    return parsed.drawnCardNames?.length || 0;
  } catch {
    return 0;
  }
}

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

// Load a deck + toggle backgrounds
function loadDeck(deckName) {
  currentDeck = deckName;

  // Reset all card state/UI before loading
  currentCard = null;
  availableCards = [];
  drawnCards = [];

  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  tarotCardWrapper?.classList.remove("awaiting-reveal");
  tarotCardElement?.classList.remove("flipped");

  const cardBack = tarotCardElement ? tarotCardElement.querySelector(".card-back") : null;
  if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
  const cardFront = document.getElementById("cardFront");
  if (cardFront) cardFront.innerHTML = "";
  document.getElementById("interpretation").classList.remove("show");
  document.getElementById("interpretationText").textContent = "";
  drawButton.textContent = "Draw Your Card";
  drawButton.disabled = false;

  // reset click readiness
  readyToReveal = false;
  unbindRevealHandler();

  updateDeckCounter();
  updateDrawnCardsList();

  // Toggle backgrounds
  if (deckName === 'emoji-matrix') {
    createMatrixCanvas();
    clearStars(); // CSS likely hides, but we also clear to be safe
  } else {
    destroyMatrixCanvas();
    createStars();
  }

  // Theme swap
  themeLink.href = 'decks/' + deckName + '/style.css';
  setLoadingState(true);

  // Remove previous deckData and script
  try { delete window.deckData; } catch (e) { window.deckData = undefined; }
  const oldScripts = document.querySelectorAll('script[data-deck]');
  oldScripts.forEach(s => s.remove());

  const script = document.createElement('script');
  script.src = 'decks/' + deckName + '/deck.js';
  script.dataset.deck = deckName;
  script.onload = function() {
    if (!window.deckData || !window.deckData.cards) {
      alert('Failed to load deck data.');
      setLoadingState(true);
      return;
    }
    tarotCards = window.deckData.cards;

    // Try to load saved state, otherwise fresh shuffle
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

// --- Events ---
drawButton.addEventListener("click", drawCard);
shuffleButton.addEventListener("click", shuffleDeck);

deckSelector.addEventListener("change", function(e) {
  // If a card is drawn but not revealed, prompt
  const tarotCardElement = document.getElementById("tarotCard");
  const awaitingReveal = readyToReveal && tarotCardElement && !tarotCardElement.classList.contains("flipped");

  if (awaitingReveal && currentCard) {
    const keep = confirm("You have a drawn card that hasn't been revealed yet. Keep it (move to drawn) or discard?\n\nOK = Keep, Cancel = Discard");
    if (keep) {
      const idx = availableCards.findIndex(c => c.name === currentCard.name);
      if (idx > -1) availableCards.splice(idx, 1);
      drawnCards.unshift(currentCard);
    } else {
      currentCard = null;
    }
    saveStateToLocalStorage();
    deckSelector.disabled = false;
  } else {
    // Save state of current deck before switching
    saveStateToLocalStorage();
  }

  // Reset face/interpretation before loading
  currentCard = null;
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  tarotCardWrapper?.classList.remove("awaiting-reveal");

  const cardBack = tarotCardElement.querySelector(".card-back");
  if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
  document.getElementById("cardFront").innerHTML = "";
  document.getElementById("interpretation").classList.remove("show");
  document.getElementById("interpretationText").textContent = "";

  drawButton.textContent = "Draw Your Card";
  drawButton.disabled = false;

  readyToReveal = false;
  unbindRevealHandler();

  // Load new deck
  loadDeck(e.target.value);
});

// --- Boot ---
setLoadingState(true);
loadDeck(determineInitialDeck());
