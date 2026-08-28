(() => {
  const post = (payload) => {
    if (window.chrome && chrome.webview) {
      chrome.webview.postMessage(payload);
    }
  };

  const master = document.getElementById("master");
  const masterLabel = document.getElementById("masterLabel");
  const cps = document.getElementById("cps");
  const cpsValue = document.getElementById("cpsValue");
  const humanized = document.getElementById("humanized");
  const overlay = document.getElementById("overlay");
  const status = document.getElementById("status");
  const canvas = document.getElementById("canvas");

  const colors = ["#00E676", "#29B6F6", "#E040FB", "#FF5722", "#FFEB3B"];

  document.querySelectorAll(".tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
      document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
      tab.classList.add("active");
      document.getElementById(`panel-${tab.dataset.tab}`).classList.add("active");
    });
  });

  master.addEventListener("change", () => {
    const on = master.checked;
    masterLabel.textContent = on ? "LIGADO" : "DESLIGADO";
    post({ type: "setMaster", value: on });
  });

  cps.addEventListener("input", () => {
    cpsValue.textContent = `${cps.value} CPS`;
    post({ type: "setCps", value: Number(cps.value) });
  });

  humanized.addEventListener("change", () => {
    post({ type: "setHumanized", value: humanized.checked });
  });

  overlay.addEventListener("change", () => {
    post({ type: "setOverlay", value: overlay.checked });
  });

  document.querySelectorAll("[data-trigger]").forEach((el) => {
    el.addEventListener("change", () => {
      post({ type: "setTrigger", button: el.dataset.trigger, value: el.checked });
    });
  });

  const spawnRipple = (x, y) => {
    const color = colors[Math.floor(Math.random() * colors.length)];
    const ripple = document.createElement("span");
    ripple.className = "ripple";
    ripple.style.left = `${x}px`;
    ripple.style.top = `${y}px`;
    ripple.style.setProperty("--c", color);
    const dot = document.createElement("span");
    dot.className = "ripple-dot";
    dot.style.left = `${x}px`;
    dot.style.top = `${y}px`;
    dot.style.setProperty("--c", color);
    canvas.appendChild(dot);
    canvas.appendChild(ripple);
    setTimeout(() => {
      ripple.remove();
      dot.remove();
    }, 950);
  };

  canvas.addEventListener("mousedown", (ev) => {
    const rect = canvas.getBoundingClientRect();
    spawnRipple(ev.clientX - rect.left, ev.clientY - rect.top);
  });

  const setStatus = (state) => {
    status.dataset.state = state;
    if (state === "active") status.textContent = "Status: ATIVO";
    else if (state === "off") status.textContent = "Status: DESLIGADO GERAL";
    else status.textContent = "Status: INATIVO";
  };

  if (window.chrome && chrome.webview) {
    chrome.webview.addEventListener("message", (event) => {
      const msg = event.data;
      if (!msg || typeof msg !== "object") return;
      if (msg.type === "status") setStatus(msg.value);
    });
  }

  post({ type: "ready" });
})();
