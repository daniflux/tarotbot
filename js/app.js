// js/app.js — TarotBot: Emoji Oracle (Restored Logic)

// --- DOM References ---
const drawButton = document.getElementById("drawButton");
const shuffleButton = document.getElementById("shuffleButton");
const deckSelector = document.getElementById("deckSelector");
const themeLink = document.getElementById("deckTheme");
const deckCounter = document.getElementById("deckCounter");

// --- State ---
let tarotCards = [];
let availableCards = [];
let drawnCards = [];
let currentCard = null;
let isDrawing = false;
let currentDeck = 'emoji';

// --- Reveal Logic (Restored to Original Pattern) ---
let readyToReveal = false;
let revealHandlerBound = false;

function onCardRevealClick() {
  if (!readyToReveal) return;
  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  const interpretationElement = document.getElementById("interpretation");
  const interpretationTextElement = document.getElementById("interpretationText");

  if (!tarotCardElement || tarotCardElement.classList.contains("flipped")) return;

  readyToReveal = false; 
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
  drawButton.disabled = availableCards.length === 0;
  if(availableCards.length === 0) {
      drawButton.textContent = "Deck Empty";
  } else {
      drawButton.textContent = "Draw Next Card";
  }

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

// --- Main Actions ---

function drawCard() {
  if (availableCards.length === 0 || isDrawing) return;
  isDrawing = true;

  const tarotCardElement = document.getElementById("tarotCard");
  const tarotCardWrapper = document.getElementById("tarotCardWrapper");
  const cardBack = tarotCardElement.querySelector(".card-back");
  const interpretationElement = document.getElementById("interpretation");
  const interpretationTextElement = document.getElementById("interpretationText");

  // Visual Reset
  const wasFlipped = tarotCardElement.classList.contains("flipped");
  tarotCardElement.classList.remove("flipped");
  drawButton.disabled = true;

  const prep = () => {
    // Pick random card
    const randomIndex = Math.floor(Math.random() * availableCards.length);
    currentCard = availableCards[randomIndex];

    // Populate Front
    displayCard(currentCard);

    // Reset Back with "Click to Reveal"
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

    // Arm the click handler
    readyToReveal = !!currentCard;
    unbindRevealHandler(); // Clear any old ones
    bindRevealHandler();   // Attach fresh one

    // Hide interpretation
    interpretationElement.classList.remove("show");
    interpretationTextElement.textContent = "";

    // Lock deck switch
    deckSelector.disabled = true;

    // Update Button
    drawButton.textContent = "Reveal Card Above";
    drawButton.disabled = true; // Button disabled so they click the card

    isDrawing = false;
    saveStateToLocalStorage();
  };

  if (wasFlipped) {
    setTimeout(prep, 250);
  } else {
    prep();
  }
}

function shuffleDeck() {
  if (confirm("Reshuffle the entire deck? This clears your current spread.")) {
    availableCards = [...tarotCards];
    drawnCards = [];
    currentCard = null;
    deckSelector.disabled = false;
    localStorage.removeItem('tarotState_' + currentDeck);
    
    updateDeckCounter();
    updateDrawnCardsList();

    const tarotCardElement = document.getElementById("tarotCard");
    const tarotCardWrapper = document.getElementById("tarotCardWrapper");
    
    tarotCardElement.classList.remove("flipped");
    tarotCardWrapper?.classList.remove("awaiting-reveal");

    // Clean faces
    const cardBack = tarotCardElement.querySelector(".card-back");
    if (cardBack) cardBack.innerHTML = '<div class="back-pattern">🌙</div>';
    document.getElementById("cardFront").innerHTML = "";
    document.getElementById("interpretation").classList.remove("show");
    document.getElementById("interpretationText").textContent = "";

    drawButton.textContent = "Draw Your Card";
    drawButton.disabled = false;

    readyToReveal = false;
    unbindRevealHandler();
    saveStateToLocalStorage();
  }
}

// --- Helpers ---

function displayCard(card) {
  const cardFront = document.getElementById("cardFront");
  if (card.image) {
    cardFront.innerHTML =
      '<img src="' + card.image + '" alt="' + card.name + '" style="width: 100%; border-radius: 12px; max-height: 250px; object-fit: contain;">' +
      '<div class="card-name">' + card.name + '</div>';
  } else {
    cardFront.innerHTML =
      '<div class="card-symbol">' + (card.symbol || '🔮') + '</div>' +
      '<div class="card-name">' + card.name + '</div>' +
      '<div class="card-meaning">' + (card.meaning || '') + '</div>';
  }
}

function updateDeckCounter() {
  deckCounter.textContent = `Cards Remaining: ${availableCards.length}/${tarotCards.length}`;
}

function updateDrawnCardsList() {
  const drawnList = document.getElementById('drawnList');
  if (drawnCards.length === 0) {
    drawnList.innerHTML = '<p class="empty-state">No cards drawn yet</p>';
    return;
  }
  drawnList.innerHTML = drawnCards.map(card => `
    <div class="drawn-card-item">
      <div class="drawn-card-symbol">${card.symbol || '🔮'}</div>
      <div class="drawn-card-info">
        <div class="drawn-card-name">${card.name}</div>
        <div class="drawn-card-meaning">${card.meaning}</div>
      </div>
    </div>
  `).join('');
}

// --- Storage ---
function saveStateToLocalStorage() {
  const state = {
    availableCardNames: availableCards.map(c => c.name),
    drawnCardNames: drawnCards.map(c => c.name),
    currentCardName: currentCard ? currentCard.name : null,
    readyToReveal: readyToReveal
  };
  localStorage.setItem("tarotState_" + currentDeck, JSON.stringify(state));
}

function loadStateFromLocalStorage() {
  try {
    const saved = localStorage.getItem("tarotState_" + currentDeck);
    if (!saved) return;
    const state = JSON.parse(saved);
    
    availableCards = tarotCards.filter(c => state.availableCardNames.includes(c.name));
    drawnCards = state.drawnCardNames.map(name => tarotCards.find(c => c.name === name)).filter(Boolean);
    currentCard = tarotCards.find(c => c.name === state.currentCardName);
    readyToReveal = state.readyToReveal || false;

    if (currentCard) {
      displayCard(currentCard);
      // Restore state visually
      const tarotCardWrapper = document.getElementById("tarotCardWrapper");
      const cardBack = document.querySelector(".card-back");
      const tarotCardElement = document.getElementById("tarotCard");

      if (readyToReveal) {
        // Was waiting to reveal
        tarotCardWrapper.classList.add("awaiting-reveal");
        cardBack.innerHTML = '<div class="back-pattern">🌙</div><div class="reveal-text">Click to reveal</div>';
        drawButton.textContent = "Reveal Card Above";
        drawButton.disabled = true;
        unbindRevealHandler();
        bindRevealHandler();
      } else {
        // Was already revealed
        tarotCardElement.classList.add("flipped");
        document.getElementById("interpretationText").textContent = currentCard.interpretation;
        document.getElementById("interpretation").classList.add("show");
        drawButton.textContent = "Draw Next Card";
      }
    }
    updateDeckCounter();
    updateDrawnCardsList();
  } catch (e) {
    console.error("State load error", e);
  }
}

// --- Deck Loading & Effects ---

function loadDeck(deckName) {
  currentDeck = deckName;
  localStorage.setItem('lastSelectedDeck', deckName);
  
  // Cleanup visual
  const cardElement = document.getElementById("tarotCard");
  const wrapper = document.getElementById("tarotCardWrapper");
  cardElement.classList.remove("flipped");
  wrapper.classList.remove("awaiting-reveal");
  document.getElementById("cardFront").innerHTML = "";
  document.querySelector(".card-back").innerHTML = '<div class="back-pattern">🌙</div>';
  document.getElementById("interpretation").classList.remove("show");
  
  drawButton.textContent = "Draw Your Card";
  drawButton.disabled = false;
  readyToReveal = false;
  unbindRevealHandler();

  // Theme & Effects
  themeLink.href = `decks/${deckName}/style.css`;
  
  if (deckName === 'emoji-matrix') {
    startMatrixEffect();
    clearStars();
  } else {
    stopMatrixEffect();
    createStars();
  }

  // Load Script
  const oldScripts = document.querySelectorAll('script[data-deck-script]');
  oldScripts.forEach(s => s.remove());
  try { delete window.deckData; } catch (e) {}

  const script = document.createElement('script');
  script.src = `decks/${deckName}/deck.js?v=${Date.now()}`;
  script.setAttribute('data-deck-script', 'true');
  script.onload = function() {
    if (!window.deckData) { alert("Error loading deck"); return; }
    tarotCards = window.deckData.cards;
    
    if (localStorage.getItem("tarotState_" + currentDeck)) {
      loadStateFromLocalStorage();
    } else {
      availableCards = [...tarotCards];
      drawnCards = [];
      updateDeckCounter();
      updateDrawnCardsList();
    }
  };
  document.body.appendChild(script);
}

function determineInitialDeck() {
  const saved = localStorage.getItem('lastSelectedDeck');
  // Check if option exists in dropdown
  const options = Array.from(deckSelector.options).map(o => o.value);
  if (saved && options.includes(saved)) return saved;
  return 'emoji';
}

// Listeners
drawButton.addEventListener("click", drawCard);
shuffleButton.addEventListener("click", shuffleDeck);
deckSelector.addEventListener("change", (e) => loadDeck(e.target.value));

// Backgrounds
function createStars() {
  const s = document.getElementById('stars');
  if(!s) return;
  s.style.display = 'block';
  s.innerHTML = '';
  for(let i=0; i<50; i++) {
    const d = document.createElement('div');
    d.className = 'star';
    d.innerHTML = '✦';
    d.style.left = Math.random()*100+'%';
    d.style.top = Math.random()*100+'%';
    d.style.animationDelay = Math.random()*3+'s';
    d.style.fontSize = (Math.random()*0.5+0.5)+'rem';
    s.appendChild(d);
  }
}
function clearStars() {
  const s = document.getElementById('stars');
  if(s) s.style.display = 'none';
}

// Matrix
const MatrixEffect = {
  canvas: null, ctx: null, cols: [], id: null,
  init() {
    if(this.canvas) return;
    this.canvas = document.createElement('canvas');
    this.canvas.id = 'matrixCanvas';
    Object.assign(this.canvas.style, {position:'fixed', inset:0, zIndex:'-1', background:'#000'});
    document.body.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');
    window.addEventListener('resize', ()=>this.resize());
    this.resize();
    this.loop();
  },
  resize() {
    if(!this.canvas) return;
    this.canvas.width = window.innerWidth;
    this.canvas.height = window.innerHeight;
    const size = Math.max(14, Math.floor(window.innerWidth/80));
    this.ctx.font = size+'px monospace';
    const numCols = Math.ceil(this.canvas.width/size);
    this.cols = Array(numCols).fill(0).map(()=>({y:Math.random()*-100, s:1+Math.random()*2, sz: size}));
  },
  loop() {
    if(!this.ctx) return;
    this.ctx.fillStyle = "rgba(0,0,0,0.05)";
    this.ctx.fillRect(0,0,this.canvas.width,this.canvas.height);
    this.ctx.fillStyle = "#00ff66";
    this.cols.forEach((col,i)=>{
      const char = String.fromCharCode(0x30A0+Math.random()*96);
      this.ctx.fillText(char, i*col.sz, col.y*col.sz);
      if(col.y*col.sz > this.canvas.height && Math.random()>0.975) col.y=0;
      col.y += col.s*0.5;
    });
    this.id = requestAnimationFrame(()=>this.loop());
  },
  destroy() {
    cancelAnimationFrame(this.id);
    if(this.canvas) this.canvas.remove();
    this.canvas = null; this.ctx = null;
  }
};
function startMatrixEffect(){ MatrixEffect.init(); }
function stopMatrixEffect(){ MatrixEffect.destroy(); }

// Boot
loadDeck(determineInitialDeck());
