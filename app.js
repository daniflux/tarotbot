(() => {
  "use strict";

  const STORAGE_VERSION = 1;
  const deckNames = ["emoji", "rider-waite", "emoji-matrix"];
  const elements = {
    deckSelector: document.getElementById("deckSelector"),
    readerMode: document.getElementById("readerMode"),
    reversals: document.getElementById("reversals"),
    focusMode: document.getElementById("focusMode"),
    cardButton: document.getElementById("cardButton"),
    card: document.getElementById("card"),
    cardFront: document.getElementById("cardFront"),
    cardPrompt: document.getElementById("cardPrompt"),
    deckStatus: document.getElementById("deckStatus"),
    nextButton: document.getElementById("nextButton"),
    shuffleButton: document.getElementById("shuffleButton"),
    meaningPanel: document.getElementById("meaningPanel"),
    meaningTitle: document.getElementById("meaningTitle"),
    meaningKeywords: document.getElementById("meaningKeywords"),
    meaningText: document.getElementById("meaningText"),
    historyToggle: document.getElementById("historyToggle"),
    historyList: document.getElementById("historyList"),
    shuffleDialog: document.getElementById("shuffleDialog")
  };
  elements.appEyebrow = document.getElementById("appEyebrow");
  elements.appTagline = document.getElementById("appTagline");
  elements.matrixRain = document.getElementById("matrixRain");
  elements.emojiStars = document.getElementById("emojiStars");

  let deck = [];
  let order = [];
  let drawn = [];
  let pending = null;
  let requestedReversalSetting = null;
  let isTransitioning = false;
  let currentDeck = localStorage.getItem("tarotbot:lastDeck") || "emoji";
  let loadToken = 0;

  if (!deckNames.includes(currentDeck)) currentDeck = "emoji";
  elements.deckSelector.value = currentDeck;

  function cardId(card) {
    return card.id || card.name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/(^-|-$)/g, "");
  }

  function randomIndex(max) {
    if (max <= 1) return 0;
    if (window.crypto?.getRandomValues) {
      const limit = Math.floor(0x100000000 / max) * max;
      const values = new Uint32Array(1);
      do window.crypto.getRandomValues(values); while (values[0] >= limit);
      return values[0] % max;
    }
    return Math.floor(Math.random() * max);
  }

  function shuffledCards(cards, includeReversals) {
    const result = cards.map(card => ({ id: cardId(card), reversed: includeReversals && randomIndex(2) === 1 }));
    for (let i = result.length - 1; i > 0; i -= 1) {
      const j = randomIndex(i + 1);
      [result[i], result[j]] = [result[j], result[i]];
    }
    return result;
  }

  function storageKey() {
    return `tarotbot:reader-deck:${currentDeck}`;
  }

  function save() {
    localStorage.setItem(storageKey(), JSON.stringify({
      version: STORAGE_VERSION,
      order,
      drawn,
      pending,
      reversals: elements.reversals.checked
    }));
    localStorage.setItem("tarotbot:lastDeck", currentDeck);
    localStorage.setItem("tarotbot:readerMode", String(elements.readerMode.checked));
    localStorage.setItem("tarotbot:focusMode", String(elements.focusMode.checked));
  }

  function restore() {
    const ids = new Set(deck.map(cardId));
    try {
      const state = JSON.parse(localStorage.getItem(storageKey()));
      const entriesAreValid = list => Array.isArray(list) && list.every(item => item && ids.has(item.id) && typeof item.reversed === "boolean");
      if (!state || state.version !== STORAGE_VERSION || !entriesAreValid(state.order) || !entriesAreValid(state.drawn)) return false;
      if (state.pending && (!ids.has(state.pending.id) || typeof state.pending.reversed !== "boolean")) return false;
      const allIds = [...state.order, ...state.drawn, ...(state.pending ? [state.pending] : [])].map(item => item.id);
      if (allIds.length !== deck.length || new Set(allIds).size !== deck.length) return false;
      order = state.order;
      drawn = state.drawn;
      pending = state.pending || null;
      elements.reversals.checked = Boolean(state.reversals);
      return true;
    } catch {
      return false;
    }
  }

  function resolveCard(entry) {
    return entry ? deck.find(card => cardId(card) === entry.id) : null;
  }

  function normalizeImagePath(image) {
    if (!image) return "";
    return image.replace(/^\.\.\//, "");
  }

  function riderImageForCard(name) {
    const majors = {
      "The Fool": "fool", "The Magician": "magician", "The High Priestess": "priestess",
      "The Empress": "empress", "The Emperor": "emperor", "The Hierophant": "hierophant",
      "The Lovers": "lovers", "The Chariot": "chariot", "Strength": "strength",
      "The Hermit": "hermit", "Wheel of Fortune": "fortune", "Justice": "justice",
      "The Hanged Man": "hanged", "Death": "death", "Temperance": "temperance",
      "The Devil": "devil", "The Tower": "tower", "The Star": "star",
      "The Moon": "moon", "The Sun": "sun", "Judgement": "judgement", "The World": "world"
    };
    if (majors[name]) return `decks/rider-waite/images/major_arcana_${majors[name]}.png`;
    const match = name.match(/^(Ace|Two|Three|Four|Five|Six|Seven|Eight|Nine|Ten|Page|Knight|Queen|King) of (Wands|Cups|Swords|Pentacles)$/);
    if (!match) return "";
    const ranks = { Ace: "ace", Two: "2", Three: "3", Four: "4", Five: "5", Six: "6", Seven: "7", Eight: "8", Nine: "9", Ten: "10", Page: "page", Knight: "knight", Queen: "queen", King: "king" };
    return `decks/rider-waite/images/minor_arcana_${match[2].toLowerCase()}_${ranks[match[1]]}.png`;
  }

  function displayImageForCard(card) {
    return currentDeck === "emoji-matrix" ? riderImageForCard(card.name) : card.image;
  }

  function validateDeck(cards) {
    if (!Array.isArray(cards) || cards.length !== 78) throw new Error("A tarot deck must contain exactly 78 cards.");
    const ids = cards.map(cardId);
    if (new Set(ids).size !== ids.length) throw new Error("The deck contains duplicate card identifiers.");
  }

  function renderCard(entry, revealed) {
    const card = resolveCard(entry);
    elements.card.classList.toggle("is-revealed", revealed);
    elements.cardFront.classList.toggle("is-reversed", Boolean(entry?.reversed));
    elements.cardFront.replaceChildren();
    if (!card) return;

    const content = document.createElement("span");
    content.className = "front-content";
    const displayImage = displayImageForCard(card);
    if (displayImage) {
      const image = document.createElement("img");
      image.className = "card-image";
      image.src = normalizeImagePath(displayImage);
      image.alt = "";
      content.appendChild(image);
    } else {
      const symbol = document.createElement("span");
      symbol.className = "card-symbol";
      symbol.textContent = card.symbol || "✦";
      content.appendChild(symbol);
    }
    const name = document.createElement("span");
    name.className = "card-name";
    name.textContent = card.name;
    content.appendChild(name);
    if (entry.reversed) {
      const orientation = document.createElement("span");
      orientation.className = "orientation";
      orientation.textContent = "Reversed";
      content.appendChild(orientation);
    }
    elements.cardFront.appendChild(content);
  }

  function renderMeaning() {
    const latest = drawn[drawn.length - 1];
    const card = resolveCard(latest);
    const shouldShow = !elements.readerMode.checked && Boolean(card);
    elements.meaningPanel.hidden = !shouldShow;
    if (!shouldShow) return;
    const reversedNotes = latest.reversed ? window.reversedCardData?.[card.name] : null;
    elements.meaningTitle.textContent = `${card.name}${latest.reversed ? " — Reversed" : ""}`;
    elements.meaningKeywords.textContent = reversedNotes?.[0] || card.meaning || "";
    elements.meaningText.textContent = reversedNotes?.[1] || card.interpretation || "";
  }

  function historyThumbnail(card) {
    const thumb = document.createElement("span");
    thumb.className = "history-thumb";
    const displayImage = displayImageForCard(card);
    if (displayImage) {
      const image = document.createElement("img");
      image.src = normalizeImagePath(displayImage);
      image.alt = "";
      thumb.appendChild(image);
    } else {
      thumb.textContent = card.symbol || "✦";
    }
    return thumb;
  }

  function renderHistory() {
    elements.historyList.replaceChildren();
    if (!drawn.length) {
      const empty = document.createElement("li");
      empty.className = "empty-history";
      empty.textContent = "No cards drawn yet.";
      elements.historyList.appendChild(empty);
      return;
    }
    drawn.forEach((entry, index) => {
      const card = resolveCard(entry);
      const item = document.createElement("li");
      item.className = "history-item";
      const number = document.createElement("span");
      number.className = "draw-number";
      number.textContent = `#${index + 1}`;
      const info = document.createElement("span");
      const name = document.createElement("span");
      name.className = "history-name";
      name.textContent = card.name;
      info.appendChild(name);
      if (entry.reversed) {
        const orientation = document.createElement("span");
        orientation.className = "history-orientation";
        orientation.textContent = "Reversed";
        info.appendChild(orientation);
      }
      item.append(number, historyThumbnail(card), info);
      elements.historyList.appendChild(item);
    });
  }

  function render() {
    document.body.dataset.deck = currentDeck;
    document.body.classList.toggle("focus-mode", elements.focusMode.checked);
    const matrixMode = currentDeck === "emoji-matrix";
    elements.appEyebrow.textContent = matrixMode ? "ARCANA // TERMINAL" : "Digital tarot deck";
    elements.appTagline.textContent = matrixMode ? "78-node entropy protocol :: no duplicate returns" : "Shuffle once. Draw without repeats.";
    MatrixRain.setActive(matrixMode);
    createEmojiStars();
    const revealedEntry = !pending && drawn.length ? drawn[drawn.length - 1] : null;
    renderCard(pending || revealedEntry, Boolean(revealedEntry));
    renderHistory();
    renderMeaning();

    if (pending) {
      elements.cardPrompt.textContent = "Reveal card";
      elements.deckStatus.textContent = `${order.length} cards remain after this draw`;
      elements.cardButton.disabled = false;
      elements.cardButton.setAttribute("aria-label", "Reveal the selected card");
      elements.nextButton.disabled = true;
      elements.nextButton.textContent = "Reveal card above";
    } else if (order.length) {
      elements.cardPrompt.textContent = drawn.length ? "Draw next card" : "Draw a card";
      elements.deckStatus.textContent = `${order.length} of ${deck.length} cards remaining`;
      elements.cardButton.disabled = false;
      elements.cardButton.setAttribute("aria-label", "Draw the next card");
      elements.nextButton.disabled = false;
      elements.nextButton.textContent = drawn.length ? "Draw next card" : "Draw card";
    } else {
      elements.cardPrompt.textContent = "Deck empty";
      elements.deckStatus.textContent = `All ${deck.length} cards have been drawn`;
      elements.cardButton.disabled = true;
      elements.nextButton.disabled = true;
      elements.nextButton.textContent = "Deck empty";
    }
    elements.shuffleButton.disabled = false;
  }

  function drawNext() {
    if (pending || !order.length || isTransitioning) return;
    const nextCard = order.shift();
    const wasRevealed = elements.card.classList.contains("is-revealed");

    if (!wasRevealed) {
      pending = nextCard;
      save();
      render();
      return;
    }

    // Keep the old face mounted until it has rotated fully behind the card back.
    // Swapping faces during this motion briefly exposes the upcoming card.
    isTransitioning = true;
    elements.card.classList.remove("is-revealed");
    elements.cardButton.disabled = true;
    elements.nextButton.disabled = true;
    elements.shuffleButton.disabled = true;
    elements.deckSelector.disabled = true;
    elements.readerMode.disabled = true;
    elements.reversals.disabled = true;
    elements.focusMode.disabled = true;

    const delay = window.matchMedia("(prefers-reduced-motion: reduce)").matches ? 0 : 680;
    window.setTimeout(() => {
      pending = nextCard;
      isTransitioning = false;
      save();
      render();
      elements.deckSelector.disabled = false;
      elements.readerMode.disabled = false;
      elements.reversals.disabled = false;
      elements.focusMode.disabled = false;
    }, delay);
  }

  function reveal() {
    if (isTransitioning) return;
    if (!pending) {
      drawNext();
      return;
    }
    drawn.push(pending);
    pending = null;
    save();
    render();
  }

  function freshShuffle() {
    order = shuffledCards(deck, elements.reversals.checked);
    drawn = [];
    pending = null;
    save();
    render();
  }

  async function loadDeck(name) {
    const token = ++loadToken;
    currentDeck = name;
    elements.deckSelector.disabled = true;
    elements.nextButton.disabled = true;
    elements.cardButton.disabled = true;
    elements.deckStatus.textContent = "Loading deck…";
    document.querySelectorAll("script[data-reader-deck]").forEach(script => script.remove());
    try { delete window.deckData; } catch { window.deckData = undefined; }

    const script = document.createElement("script");
    script.dataset.readerDeck = "true";
    script.src = `decks/${name}/deck.js`;
    script.onload = () => {
      if (token !== loadToken) return;
      try {
        deck = window.deckData?.cards || [];
        validateDeck(deck);
        if (!restore()) freshShuffle();
        else render();
      } catch (error) {
        console.error(error);
        elements.deckStatus.textContent = "This deck could not be loaded.";
      } finally {
        elements.deckSelector.disabled = false;
      }
    };
    script.onerror = () => {
      if (token !== loadToken) return;
      elements.deckStatus.textContent = "This deck could not be loaded.";
      elements.deckSelector.disabled = false;
    };
    document.body.appendChild(script);
  }

  const MatrixRain = {
    active: false,
    context: null,
    columns: [],
    frame: 0,
    lastDraw: 0,
    resize() {
      const canvas = elements.matrixRain;
      const scale = window.devicePixelRatio || 1;
      canvas.width = Math.floor(window.innerWidth * scale);
      canvas.height = Math.floor(window.innerHeight * scale);
      canvas.style.width = `${window.innerWidth}px`;
      canvas.style.height = `${window.innerHeight}px`;
      this.context = canvas.getContext("2d");
      this.context.setTransform(scale, 0, 0, scale, 0, 0);
      const fontSize = 15;
      this.columns = Array.from({ length: Math.ceil(window.innerWidth / fontSize) }, () => Math.random() * -60);
    },
    draw(time = 0) {
      if (!this.active) return;
      this.frame = requestAnimationFrame(next => this.draw(next));
      if (time - this.lastDraw < 52) return;
      this.lastDraw = time;
      const ctx = this.context;
      if (!ctx) return;
      ctx.fillStyle = "rgba(0, 0, 0, 0.10)";
      ctx.fillRect(0, 0, window.innerWidth, window.innerHeight);
      ctx.font = "15px monospace";
      this.columns.forEach((position, index) => {
        const glyphs = "01{}[]<>/\\λ†☿♄♃♆ᚠᚱᚢᚾᛖᛋᛏᛟ";
        const glyph = glyphs[Math.floor(Math.random() * glyphs.length)];
        ctx.fillStyle = Math.random() > .96 ? "#c9ffd5" : "#00c832";
        ctx.fillText(glyph, index * 15, position * 15);
        this.columns[index] = position * 15 > window.innerHeight && Math.random() > .975 ? 0 : position + .58;
      });
    },
    setActive(active) {
      if (active === this.active) return;
      this.active = active;
      cancelAnimationFrame(this.frame);
      if (active) {
        this.resize();
        this.draw();
      } else if (this.context) {
        this.context.clearRect(0, 0, window.innerWidth, window.innerHeight);
      }
    }
  };

  function createEmojiStars() {
    if (elements.emojiStars.childElementCount) return;
    const symbols = ["✦", "✨", "⭐", "🌟"];
    for (let index = 0; index < 42; index += 1) {
      const star = document.createElement("span");
      star.className = "emoji-star";
      star.textContent = symbols[index % symbols.length];
      star.style.left = `${3 + Math.random() * 94}%`;
      star.style.top = `${2 + Math.random() * 94}%`;
      star.style.fontSize = `${.55 + Math.random() * .8}rem`;
      star.style.setProperty("--twinkle-speed", `${2.2 + Math.random() * 3.8}s`);
      star.style.setProperty("--twinkle-delay", `${-Math.random() * 5}s`);
      elements.emojiStars.appendChild(star);
    }
  }

  window.addEventListener("resize", () => {
    if (MatrixRain.active) MatrixRain.resize();
  });

  elements.cardButton.addEventListener("click", reveal);
  elements.nextButton.addEventListener("click", drawNext);
  elements.deckSelector.addEventListener("change", event => loadDeck(event.target.value));
  elements.readerMode.addEventListener("change", () => { save(); renderMeaning(); });
  elements.focusMode.addEventListener("change", () => { save(); render(); });
  elements.reversals.addEventListener("change", () => {
    if (drawn.length || pending) {
      requestedReversalSetting = elements.reversals.checked;
      elements.reversals.checked = !elements.reversals.checked;
      elements.shuffleDialog.showModal();
      return;
    }
    freshShuffle();
  });
  elements.shuffleButton.addEventListener("click", () => {
    requestedReversalSetting = null;
    if (!drawn.length && !pending) freshShuffle();
    else elements.shuffleDialog.showModal();
  });
  elements.shuffleDialog.addEventListener("close", () => {
    if (elements.shuffleDialog.returnValue === "confirm") {
      if (requestedReversalSetting !== null) elements.reversals.checked = requestedReversalSetting;
      freshShuffle();
    }
    requestedReversalSetting = null;
  });
  elements.historyToggle.addEventListener("click", () => {
    const expanded = elements.historyToggle.getAttribute("aria-expanded") === "true";
    elements.historyToggle.setAttribute("aria-expanded", String(!expanded));
    elements.historyToggle.textContent = expanded ? "Show" : "Hide";
    elements.historyList.hidden = expanded;
  });

  const storedReaderMode = localStorage.getItem("tarotbot:readerMode");
  if (storedReaderMode !== null) elements.readerMode.checked = storedReaderMode === "true";
  elements.focusMode.checked = localStorage.getItem("tarotbot:focusMode") === "true";
  loadDeck(currentDeck);
})();

