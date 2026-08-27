import tkinter as tk
import customtkinter as ctk
import threading
import time
import random
from collections import deque
from pynput.mouse import Controller, Listener, Button

# Configuração do tema Material Design 3 do CustomTkinter
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

class ClickEngine:
    """Motor principal que gerencia os cliques e a leitura do mouse."""
    def __init__(self, update_callback):
        self.mouse = Controller()
        self.update_callback = update_callback  # Atualiza a interface
        
        # Configurações padrão
        self.cps = 12
        self.humanized = False
        self.master_enabled = True # Permite ligar/desligar todo o sistema
        
        # Botões habilitados para ativar o autoclick (Múltiplas escolhas)
        self.active_triggers = {
            Button.left: True,
            Button.right: False,
            Button.middle: False,
            Button.x1: False,
            Button.x2: False
        }
        
        # Estado do motor
        self.running = True
        
        # --- MEDIDAS DE VERIFICAÇÃO (Correção Anti-Bug) ---
        # Substitui o "is_simulating" por rastreadores precisos.
        # Eles verificam e bloqueiam apenas as injeções da máquina, 
        # nunca perdendo a leitura do dedo do usuário soltando o botão.
        self.ignore_next_press = {b: False for b in self.active_triggers}
        self.ignore_next_release = {b: False for b in self.active_triggers}
        # --------------------------------------------------
        
        # Controles independentes por botão
        self.last_click_time = {b: 0 for b in self.active_triggers}
        self.clicking_state = {b: False for b in self.active_triggers}
        self.double_click_threshold = 0.3
        
        self.click_history = deque(maxlen=200)
        
        # Inicia as threads
        self.click_thread = threading.Thread(target=self._click_loop, daemon=True)
        self.click_thread.start()
        
        self.listener = Listener(on_click=self._on_click)
        self.listener.start()

    def _click_loop(self):
        """Loop contínuo que executa os cliques quando ativado."""
        while self.running:
            # Se o mestre estiver desligado, apenas pausa a thread
            if not self.master_enabled:
                time.sleep(0.05)
                continue

            clicked_any = False
            
            # Verifica todos os botões e clica os que estiverem ativos no estado de duplo-clique
            for btn, is_clicking in list(self.clicking_state.items()):
                if is_clicking and self.active_triggers[btn]:
                    # Sinaliza para o verificador ignorar o próximo PRESS (injetado)
                    self.ignore_next_press[btn] = True
                    self.mouse.press(btn)
                    
                    time.sleep(random.uniform(0.01, 0.02))
                    
                    # Sinaliza para o verificador ignorar o próximo RELEASE (injetado)
                    self.ignore_next_release[btn] = True
                    self.mouse.release(btn)
                    
                    self.click_history.append(time.time())
                    clicked_any = True
            
            if clicked_any:
                base_interval = 1.0 / self.cps
                if self.humanized:
                    sleep_time = random.uniform(base_interval * 0.7, base_interval * 1.3)
                else:
                    sleep_time = base_interval
                
                time.sleep(max(0.001, sleep_time - 0.025))
            else:
                time.sleep(0.01)

    def _on_click(self, x, y, button, pressed):
        """Monitora os eventos físicos do mouse."""
        if not self.master_enabled:
            return

        # Só processa se o botão estiver configurado para ser um gatilho
        if button in self.active_triggers and self.active_triggers[button]:
            if pressed:
                # Verificação: ignora APENAS o press gerado pelo próprio script
                if self.ignore_next_press.get(button, False):
                    self.ignore_next_press[button] = False
                    return

                now = time.time()
                # Verifica se foi um duplo clique rápido físico
                if now - self.last_click_time[button] < self.double_click_threshold:
                    if not self.clicking_state[button]:
                        self.clicking_state[button] = True
                        self.update_callback()
                self.last_click_time[button] = now
            else:
                # Verificação: ignora APENAS o release gerado pelo próprio script
                if self.ignore_next_release.get(button, False):
                    self.ignore_next_release[button] = False
                    return

                # Se chegou aqui, é o usuário soltando o botão de verdade.
                # Desativa o clique com precisão imediata.
                if self.clicking_state[button]:
                    self.clicking_state[button] = False
                    self.update_callback()

    def get_real_cps(self):
        """Calcula a quantidade de cliques injetados no último segundo."""
        now = time.time()
        while self.click_history and self.click_history[0] < now - 1.0:
            self.click_history.popleft()
        return len(self.click_history)

class OverlayWindow:
    """Janela flutuante transparente para mostrar o CPS."""
    def __init__(self, root):
        self.top = tk.Toplevel(root)
        self.top.overrideredirect(True)
        self.top.attributes("-topmost", True)
        
        try:
            self.top.attributes("-transparentcolor", "black")
            bg_color = "black"
        except:
            bg_color = "#1a1a1a"
            
        self.top.configure(bg=bg_color)
        
        self.label = tk.Label(self.top, text="CPS: 0", font=("Segoe UI", 28, "bold"), 
                              fg="#00E676", bg=bg_color)
        self.label.pack(padx=15, pady=15)
        
        self.top.withdraw()
        
        x = root.winfo_screenwidth() - 250
        y = 50
        self.top.geometry(f"+{x}+{y}")

    def update_text(self, text, color="#00E676"):
        self.label.configure(text=text, fg=color)

    def show(self):
        self.top.deiconify()

    def hide(self):
        self.top.withdraw()

class AutoClickerApp(ctk.CTk):
    """Interface Gráfica Principal do Auto Clicker."""
    def __init__(self):
        super().__init__()
        
        self.title("Auto Clicker M3 Pro")
        self.geometry("500x750")
        self.resizable(False, False)
        
        self.button_map = {
            "Esquerdo": Button.left,
            "Direito": Button.right,
            "Meio": Button.middle,
            "Lateral 1 (X1)": Button.x1,
            "Lateral 2 (X2)": Button.x2
        }
        
        self.engine = ClickEngine(self.on_state_change)
        self.overlay = OverlayWindow(self)
        
        self.build_ui()
        self.update_overlay_loop()

    def build_ui(self):
        # --- HEADER (Master Switch) ---
        header_frame = ctk.CTkFrame(self, fg_color="transparent")
        header_frame.pack(fill="x", padx=20, pady=(20, 10))
        
        title_lbl = ctk.CTkLabel(header_frame, text="Auto Clicker", font=ctk.CTkFont(size=26, weight="bold"))
        title_lbl.pack(side="left")
        
        self.switch_master = ctk.CTkSwitch(header_frame, text="LIGADO", font=ctk.CTkFont(weight="bold"), 
                                           command=self.on_master_toggle, progress_color="#00E676")
        self.switch_master.select()
        self.switch_master.pack(side="right", pady=5)

        # --- ABAS ---
        self.tabs = ctk.CTkTabview(self)
        self.tabs.pack(fill="both", expand=True, padx=20, pady=(0, 20))
        
        self.tab_config = self.tabs.add("Configurações")
        self.tab_test = self.tabs.add("Teste (Ripples)")
        
        self.build_config_tab()
        self.build_test_tab()
        
        # --- STATUS (Rodapé) ---
        self.lbl_status = ctk.CTkLabel(self, text="Status: INATIVO", font=ctk.CTkFont(size=18, weight="bold"), text_color="#EF5350")
        self.lbl_status.pack(pady=(0, 10))

    def build_config_tab(self):
        parent = self.tab_config
        
        # Card CPS
        card1 = ctk.CTkFrame(parent, corner_radius=15)
        card1.pack(padx=10, pady=10, fill="x")
        
        self.lbl_cps_ref = ctk.CTkLabel(card1, text="Velocidade (CPS): 12", font=ctk.CTkFont(weight="bold"))
        self.lbl_cps_ref.pack(pady=(15, 5), padx=20, anchor="w")

        self.slider_cps = ctk.CTkSlider(card1, from_=1, to=100, number_of_steps=99, command=self.on_cps_change)
        self.slider_cps.set(12)
        self.slider_cps.pack(padx=20, pady=10, fill="x")
        
        self.switch_humanized = ctk.CTkSwitch(card1, text="Modo Humanizado (Aleatoriedade)", command=self.on_humanized_toggle)
        self.switch_humanized.pack(padx=20, pady=(10, 20), anchor="w")

        # Card de Botões
        card2 = ctk.CTkFrame(parent, corner_radius=15)
        card2.pack(padx=10, pady=10, fill="x")
        
        lbl_trigger = ctk.CTkLabel(card2, text="Ativar duplo-clique para os botões:", font=ctk.CTkFont(weight="bold"))
        lbl_trigger.pack(pady=(15, 10), padx=20, anchor="w")
        
        self.trigger_vars = {}
        for name, btn in self.button_map.items():
            # Inicia o esquerdo como marcado por padrão
            var = ctk.BooleanVar(value=(btn == Button.left))
            chk = ctk.CTkCheckBox(card2, text=f"Botão {name}", variable=var, 
                                  command=lambda b=btn, v=var: self.on_trigger_toggle(b, v))
            chk.pack(padx=20, pady=5, anchor="w")
            self.trigger_vars[btn] = var
            
        ctk.CTkFrame(card2, height=10, fg_color="transparent").pack() # Padding

        # Card de Overlay
        card3 = ctk.CTkFrame(parent, corner_radius=15)
        card3.pack(padx=10, pady=10, fill="x")
        
        self.switch_overlay = ctk.CTkSwitch(card3, text="Ativar Sobreposição de Tela (Overlay)", command=self.on_overlay_toggle)
        self.switch_overlay.pack(padx=20, pady=20, anchor="w")

    def build_test_tab(self):
        parent = self.tab_test
        
        lbl_info = ctk.CTkLabel(parent, text="Dê um duplo-clique aqui para testar o CPS.\nCada clique gerará uma onda de água.", text_color="gray")
        lbl_info.pack(pady=(5, 5))
        
        # Canvas escuro para desenhar as ondas
        self.canvas = tk.Canvas(parent, bg="#121212", highlightthickness=1, highlightbackground="#333333")
        self.canvas.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Binds para detectar os cliques no Canvas
        self.canvas.bind("<Button-1>", self.on_canvas_click)
        self.canvas.bind("<Button-2>", self.on_canvas_click)
        self.canvas.bind("<Button-3>", self.on_canvas_click)

    def on_canvas_click(self, event):
        """Disparado toda vez que o canvas recebe um clique real ou injetado."""
        x, y = event.x, event.y
        colors = ["#00E676", "#29B6F6", "#E040FB", "#FF5722", "#FFEB3B"]
        color = random.choice(colors)
        
        # Desenha a pedra (centro)
        center = self.canvas.create_oval(x-2, y-2, x+2, y+2, fill=color, outline="")
        
        # Desenha a onda inicial
        ripple = self.canvas.create_oval(x-5, y-5, x+5, y+5, outline=color, width=2)
        
        # Inicia a animação da onda
        self.animate_ripple(ripple, center, x, y, 5)

    def animate_ripple(self, ripple, center, x, y, radius):
        """Faz o circulo expandir e sumir com o tempo."""
        if radius > 60:
            self.canvas.delete(ripple)
            self.canvas.delete(center)
            return
            
        self.canvas.coords(ripple, x-radius, y-radius, x+radius, y+radius)
        
        # Afina a linha para simular desaparecimento
        if radius > 40:
            self.canvas.itemconfig(ripple, width=1)
            
        self.after(20, self.animate_ripple, ripple, center, x, y, radius + 4)

    # --- Callbacks ---
    def on_master_toggle(self):
        is_on = self.switch_master.get()
        self.engine.master_enabled = is_on
        self.switch_master.configure(text="LIGADO" if is_on else "DESLIGADO", 
                                     text_color="#00E676" if is_on else "gray")
        if not is_on:
            self.lbl_status.configure(text="Status: DESLIGADO GERAL", text_color="gray")
        else:
            self.on_state_change()

    def on_trigger_toggle(self, btn, var):
        self.engine.active_triggers[btn] = var.get()

    def on_cps_change(self, value):
        cps = int(value)
        self.lbl_cps_ref.configure(text=f"Velocidade (CPS): {cps}")
        self.engine.cps = cps

    def on_humanized_toggle(self):
        self.engine.humanized = bool(self.switch_humanized.get())

    def on_overlay_toggle(self):
        if self.switch_overlay.get():
            self.overlay.show()
        else:
            self.overlay.hide()

    def on_state_change(self):
        if not self.engine.master_enabled:
            return
            
        is_active = any(self.engine.clicking_state.values())
        self.after(0, self._update_status_label, is_active)
        
    def _update_status_label(self, is_active):
        if is_active:
            self.lbl_status.configure(text="Status: ATIVO", text_color="#00E676")
        else:
            self.lbl_status.configure(text="Status: INATIVO", text_color="#EF5350")

    def update_overlay_loop(self):
        if self.switch_overlay.get():
            is_active = any(self.engine.clicking_state.values())
            if is_active:
                real_cps = self.engine.get_real_cps()
                self.overlay.update_text(f"CPS: {real_cps}", color="#00E676")
            else:
                self.overlay.update_text("CPS: 0", color="gray")
                
        self.after(100, self.update_overlay_loop)

if __name__ == "__main__":
    app = AutoClickerApp()
    app.mainloop()