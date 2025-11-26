// js/app.js — TarotBot: Emoji Oracle (Robust Reveal)

// --- DOM References ---
const ui = {
  drawButton: document.getElementById("drawButton"),
  shuffleButton: document.getElementById("shuffleButton"),
  deckSelector: document.getElementById("deckSelector"),
  themeLink: document.getElementById("deckTheme"),
  cardWrapper: document.getElementById("tarotCardWrapper"),
  cardElement: document.getElementById("tarotCard"),
  cardFront: document.getElementById("cardFront"),
  cardBack: document.querySelector(".card-back"),
  interpretationPanel: document.getElementById("interpretation"),
  interpretationText: document.getElementById("interpretationText"),
  deckCounter: document.getElementById("deckCounter"),
  drawnList: document.getElementById("drawnList")
};

// --- State ---
const state = {
  tarotCards: [],
  availableCards: [],
  drawnCards: [],
  currentCard: null,
  currentDeck: 'emoji',
  isAnimating: false,
  readyToReveal: false
};

// --- Core Functions ---

function init() {
  state.currentDeck = determineInitialDeck();
  loadDeck(state.currentDeck);
  setupEventListeners();
}

function setupEventListeners() {
  // Main Action Button (Handles both Draw and Reveal)
  ui.drawButton.addEventListener("click", () => {
    if (state.readyToReveal) {
      handleRevealClick();
    } else {
      handleDrawClick();
    }
  });

  ui.shuffleButton.addEventListener("click", handleShuffleClick);
  ui.deckSelector.addEventListener("change", handleDeckChange);
  
  // Card click for reveal
  ui.cardElement.addEventListener("click", handleRevealClick);
}

// --- Deck Loading Logic ---

function determineInitialDeck() {
  const savedDeck = localStorage.getItem('lastSelectedDeck');
  if (savedDeck) {
    const exists = Array.from(ui.deckSelector.options).some(opt => opt.value === savedDeck);
    if (exists) {
      ui.deckSelector.value = savedDeck;
      return savedDeck;
    }
  }
  return 'emoji'; 
}

function loadDeck(deckName) {
  setLoadingState(true);
  state.currentDeck = deckName;
  localStorage.setItem('lastSelectedDeck', deckName);

  // Background Effects
  if (deckName === 'emoji-matrix') {
    startMatrixEffect();
    clearStars();
  } else {
    stopMatrixEffect();
    createStars();
  }

  ui.themeLink.href = `decks/${deckName}/style.css`;

  const oldScript = document.querySelector('script[data-deck-script]');
  if (oldScript) oldScript.remove();
  try { delete window.deckData; } catch (e) { window.deckData = undefined; }

  const script = document.createElement('script');
  script.src = `decks/${deckName}/deck.js?v=${new Date().getTime()}`;
  script.setAttribute('data-deck-script', 'true');
  
  script.onload = () => {
    if (!window.deckData || !window.deckData.cards) {
      alert('Error loading deck data.');
      return;
    }
    
    state.tarotCards = [...window.deckData.cards];
    
    if (localStorage.getItem(`tarotState_${deckName}`)) {
      loadStateFromStorage();
    } else {
      resetDeckState();
    }
    setLoadingState(false);
  };
  
  document.body.appendChild(script);
}

// --- Action Handlers ---

function handleDrawClick() {
  if (state.availableCards.length === 0 || state.isAnimating || state.readyToReveal) return;

  state.isAnimating = true;
  ui.drawButton.disabled = true;

  // Visual reset
  ui.cardElement.classList.remove("flipped");
  ui.interpretationPanel.classList.remove("show");
  
  // Tiny delay to allow flip-back animation if needed
  setTimeout(() => {
    const randomIndex = Math.floor(Math.random() * state.availableCards.length);
    state.currentCard = state.availableCards[randomIndex];

    prepareCardFace(state.currentCard);
    
    // Reset Back
    ui.cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    const revealHint = document.createElement('div');
    revealHint.className = 'reveal-text';
    revealHint.textContent = 'Click to Reveal';
    ui.cardBack.appendChild(revealHint);

    ui.cardWrapper.classList.add("awaiting-reveal");
    
    // Update State
    state.readyToReveal = true;
    state.isAnimating = false;
    
    // UPDATE BUTTON: Enable it, but make it say "Reveal"
    ui.drawButton.textContent = "Reveal Card";
    ui.drawButton.disabled = false; 
    ui.drawButton.classList.add("pulse-button"); // Optional visual cue
    
    saveStateToStorage();
  }, 300);
}

function handleRevealClick() {
  if (!state.readyToReveal || state.cardElement.classList.contains("flipped")) return;

  state.readyToReveal = false;
  ui.cardWrapper.classList.remove("awaiting-reveal");
  ui.cardElement.classList.add("flipped");
  
  // Clean up button
  ui.drawButton.classList.remove("pulse-button");
  ui.drawButton.disabled = true; // Disable briefly while processing

  // Move card logic
  const idx = state.availableCards.findIndex(c => c.name === state.currentCard.name);
  if (idx > -1) state.availableCards.splice(idx, 1);
  state.drawnCards.unshift(state.currentCard);

  updateUI();
  saveStateToStorage();

  // Show interpretation
  setTimeout(() => {
    if (state.currentCard) {
      ui.interpretationText.textContent = state.currentCard.interpretation;
      ui.interpretationPanel.classList.add("show");
    }
    
    // Reset button for next draw
    ui.drawButton.disabled = state.availableCards.length === 0;
    ui.drawButton.textContent = state.availableCards.length === 0 ? "Deck Empty" : "Draw Next Card";
    ui.deckSelector.disabled = false;
  }, 600);
}

function handleShuffleClick() {
  if (confirm("Reshuffle the entire deck? This clears your current spread.")) {
    resetDeckState();
    localStorage.removeItem(`tarotState_${state.currentDeck}`);
  }
}

function handleDeckChange(e) {
  const newDeck = e.target.value;
  if (state.currentCard && state.readyToReveal) {
    const confirmSwitch = confirm("You have a card waiting to be revealed. Switching decks will discard it. Continue?");
    if (!confirmSwitch) {
      ui.deckSelector.value = state.currentDeck;
      return;
    }
  }
  loadDeck(newDeck);
}

// --- Helpers ---

function resetDeckState() {
  state.availableCards = [...state.tarotCards];
  state.drawnCards = [];
  state.currentCard = null;
  state.readyToReveal = false;
  
  ui.cardElement.classList.remove("flipped");
  ui.cardWrapper.classList.remove("awaiting-reveal");
  ui.interpretationPanel.classList.remove("show");
  ui.interpretationText.textContent = "";
  ui.cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
  ui.cardFront.innerHTML = '';
  ui.drawButton.classList.remove("pulse-button");
  
  updateUI();
  ui.drawButton.disabled = false;
  ui.drawButton.textContent = "Draw Your Card";
}

function prepareCardFace(card) {
  // Handles both image decks and emoji decks safely
  if (card.image) {
    ui.cardFront.innerHTML = `
      <img src="${card.image}" alt="${card.name}" style="width: 100%; border-radius: 12px; max-height: 250px; object-fit: contain;">
      <div class="card-name">${card.name}</div>
    `;
  } else {
    ui.cardFront.innerHTML = `
      <div class="card-symbol">${card.symbol || '🔮'}</div>
      <div class="card-name">${card.name}</div>
      <div class="card-meaning">${card.meaning || ''}</div>
    `;
  }
}

function updateUI() {
  ui.deckCounter.textContent = `Cards Remaining: ${state.availableCards.length}/${state.tarotCards.length}`;
  
  if (state.drawnCards.length === 0) {
    ui.drawnList.innerHTML = '<p class="empty-state">No cards drawn yet</p>';
  } else {
    ui.drawnList.innerHTML = state.drawnCards.map(card => `
      <div class="drawn-card-item">
        <div class="drawn-card-symbol">${card.symbol || '🔮'}</div>
        <div class="drawn-card-info">
          <div class="drawn-card-name">${card.name}</div>
          <div class="drawn-card-meaning">${card.meaning}</div>
        </div>
      </div>
    `).join('');
  }
}

function setLoadingState(isLoading) {
  ui.drawButton.disabled = isLoading;
  ui.shuffleButton.disabled = isLoading;
  ui.deckSelector.disabled = isLoading;
  if (isLoading) ui.deckCounter.textContent = "Loading deck...";
}

// --- Persistence ---

function saveStateToStorage() {
  const data = {
    availableNames: state.availableCards.map(c => c.name),
    drawnNames: state.drawnCards.map(c => c.name),
    currentCardName: state.currentCard ? state.currentCard.name : null,
    readyToReveal: state.readyToReveal
  };
  localStorage.setItem(`tarotState_${state.currentDeck}`, JSON.stringify(data));
}

function loadStateFromStorage() {
  try {
    const raw = localStorage.getItem(`tarotState_${state.currentDeck}`);
    if (!raw) return;
    
    const saved = JSON.parse(raw);
    state.availableCards = state.tarotCards.filter(c => saved.availableNames.includes(c.name));
    state.drawnCards = saved.drawnNames.map(n => state.tarotCards.find(c => c.name === n)).filter(Boolean);
    state.readyToReveal = saved.readyToReveal || false;
    
    if (saved.currentCardName) {
      state.currentCard = state.tarotCards.find(c => c.name === saved.currentCardName);
      prepareCardFace(state.currentCard);
      
      if (state.readyToReveal) {
        ui.cardWrapper.classList.add("awaiting-reveal");
        ui.cardBack.innerHTML = '<div class="back-pattern">🌙</div><div class="reveal-text">Click to Reveal</div>';
        ui.drawButton.textContent = "Reveal Card";
        ui.drawButton.classList.add("pulse-button");
        ui.drawButton.disabled = false;
      } else {
        ui.cardElement.classList.add("flipped");
        ui.interpretationText.textContent = state.currentCard.interpretation;
        ui.interpretationPanel.classList.add("show");
        ui.drawButton.textContent = "Draw Next Card";
      }
    } else {
      ui.drawButton.textContent = "Draw Your Card";
    }
    updateUI();
  } catch (e) {
    console.error("Save state corruption", e);
    resetDeckState();
  }
}

// --- Background Effects ---

function createStars() {
  const container = document.getElementById('stars');
  if (!container) return;
  container.innerHTML = '';
  container.style.display = 'block';
  for (let i = 0; i < 50; i++) {
    const star = document.createElement('div');
    star.className = 'star';
    star.innerHTML = '✦';
    star.style.left = Math.random() * 100 + '%';
    star.style.top = Math.random() * 100 + '%';
    star.style.animationDelay = Math.random() * 3 + 's';
    star.style.fontSize = (Math.random() * 0.5 + 0.5) + 'rem';
    container.appendChild(star);
  }
}

function clearStars() {
  const container = document.getElementById('stars');
  if (container) container.style.display = 'none';
}

const MatrixEffect = {
  canvas: null,
  ctx: null,
  columns: [],
  fontSize: 16,
  animationId: null,
  init() {
    if (this.canvas) return;
    this.canvas = document.createElement('canvas');
    this.canvas.id = 'matrixCanvas';
    Object.assign(this.canvas.style, {
      position: 'fixed', inset: '0', zIndex: '-1', background: '#000'
    });
    document.body.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');
    window.addEventListener('resize', () => this.resize());
    this.resize();
    this.loop();
  },
  resize() {
    if (!this.canvas) return;
    this.canvas.width = window.innerWidth;
    this.canvas.height = window.innerHeight;
    this.fontSize = Math.max(14, Math.floor(window.innerWidth / 80));
    this.ctx.font = `${this.fontSize}px monospace`;
    const cols = Math.ceil(this.canvas.width / this.fontSize);
    this.columns = Array(cols).fill(0).map(() => ({ y: Math.random() * -100, speed: 1 + Math.random() * 2 }));
  },
  loop() {
    if (!this.ctx) return;
    this.ctx.fillStyle = "rgba(0, 0, 0, 0.05)";
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    this.ctx.fillStyle = "#00ff66";
    this.columns.forEach((col, i) => {
      const char = String.fromCharCode(0x30A0 + Math.random() * 96);
      this.ctx.fillText(char, i * this.fontSize, col.y * this.fontSize);
      if (col.y * this.fontSize > this.canvas.height && Math.random() > 0.975) col.y = 0;
      col.y += col.speed * 0.5;
    });
    this.animationId = requestAnimationFrame(() => this.loop());
  },
  destroy() {
    if (this.animationId) cancelAnimationFrame(this.animationId);
    if (this.canvas) this.canvas.remove();
    this.canvas = null;
    this.ctx = null;
  }
};

function startMatrixEffect() { MatrixEffect.init(); }
function stopMatrixEffect() { MatrixEffect.destroy(); }

init();
