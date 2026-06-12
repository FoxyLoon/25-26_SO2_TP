import sys
import subprocess
import tkinter as tk
from tkinter import messagebox

# --- GESTÃO AUTOMÁTICA DE DEPENDÊNCIAS ---
# Lista de (nome_do_pacote, descricao) a verificar
DEPENDENCIAS = [
    ("markdown", "formatação Markdown"),
    ("tkhtmlview", "renderização HTML no tkinter"),
]

def _verificar_dependencias():
    """Devolve a lista de dependências em falta."""
    em_falta = []
    for package, descricao in DEPENDENCIAS:
        try:
            __import__(package)
        except ImportError:
            em_falta.append((package, descricao))
    return em_falta

def _dialog_instalacao(em_falta):
    """Mostra um diálogo tkinter a perguntar se o utilizador quer instalar as dependências."""
    root = tk.Tk()
    root.withdraw()  # esconder janela principal

    lista = "\n".join(f"  • {pkg} ({desc})" for pkg, desc in em_falta)
    msg = (
        "As seguintes dependências não foram detetadas:\n\n"
        f"{lista}\n\n"
        "Desejas instalá-las automaticamente?"
    )
    resposta = messagebox.askyesno("Dependências em falta", msg)
    root.destroy()
    return resposta

def _instalar_pacotes(packages):
    """Instala os pacotes pedidos via pip."""
    print(f"[pip] A instalar: {', '.join(packages)}...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", *packages])
    except subprocess.CalledProcessError:
        print(f"[!] Erro ao instalar os pacotes. Instala manualmente:")
        for pkg in packages:
            print(f"    pip install {pkg}")
        sys.exit(1)

# Verificar e instalar
deps_falta = _verificar_dependencias()
if deps_falta:
    pacotes = [p[0] for p in deps_falta]
    if _dialog_instalacao(deps_falta):
        _instalar_pacotes(pacotes)
    else:
        messagebox.showerror("Dependências em falta",
            "Instala as dependências listadas e executa novamente.\n\n"
            "pip install " + " ".join(pacotes))
        sys.exit(1)

# Importar após instalação
try:
    import markdown
    from tkhtmlview import HTMLText
except ImportError as e:
    print(f"[!] Falha ao importar após instalação: {e}")
    sys.exit(1)
# ------------------------------------------

import os
import random
import re
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText

# Diretório do script — permite lançar de qualquer pasta
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Tentar importar OpenAI (opcional — pergunta ao utilizador via diálogo)
IA_DISPONIVEL = False
try:
    from openai import OpenAI
    IA_DISPONIVEL = True
except ImportError:
    resposta = messagebox.askyesno(
        "OpenAI deteção",
        "O pacote `openai` (cliente IA) não foi detetado.\n\n"
        "Desejas instalá-lo automaticamente para ativar a avaliação por IA?"
    )
    if resposta:
        _instalar_pacotes(["openai"])
        try:
            from openai import OpenAI
            IA_DISPONIVEL = True
        except ImportError:
            print("[openai] Instale concluído mas import falhou. IA desativada.")
    else:
        print("[openai] IA desativada pelo utilizador.")

# Configuração do cliente para o LM Studio local (só usada se IA_DISPONIVEL)
if IA_DISPONIVEL:
    client = OpenAI(
        base_url="http://localhost:1234/v1",
        api_key="lm-studio"
    )
    MODELO_LM_STUDIO = "qwen/qwen3.6-35b-a3b"

class AplicacaoEstudos:
    def __init__(self, root):
        self.root = root
        self.root.title("Avaliador de Estudos - Dark Mode")
        self.root.geometry("1100x850")
        self.root.minsize(850, 700)
        
        # Estado IA
        self.ia_ativada = False
        self.ultimo_markdown_ia = "# Instruções\nEscreve a tua resposta acima e submete para obter o teu feedback técnico."
        
        # Carregar as perguntas do ficheiro de revisão
        caminho_perguntas = os.path.join(_SCRIPT_DIR, "Perguntas_Definicao.md")
        self.perguntas = self.carregar_perguntas_estruturadas(caminho_perguntas)
        self.pergunta_atual = None
        self.pergunta_numero = 0
        
        if not self.perguntas:
            messagebox.showerror("Erro", f"Não foi possível carregar o ficheiro '{caminho_perguntas}'.")
            self.root.destroy()
            return
        
        # Pergunta atual para avaliação IA (não a aleatória)
        self.pergunta_ia = None
        
        # Mostrar diálogo de configuração inicial
        self.configurar_inicial()
        
        if not self.ia_ativada:
            self.root.title("Avaliador de Estudos - Modo Manual")
        
        self.configurar_estilos_dark()
        self.criar_interface()
        self.proxima_pergunta()

    def configurar_inicial(self):
        """Diálogo inicial para ativar/desativar a IA."""
        dialog = tk.Toplevel(self.root)
        dialog.title("Configuração Inicial")
        dialog.geometry("450x300")
        dialog.transient(self.root)
        dialog.grab_set()
        
        ttk.Label(dialog, text="Configuração do Avaliador de Estudos", 
                  font=("Helvetica", 13, "bold")).pack(pady=(15, 5))
        
        ttk.Label(dialog, text="Ativar avaliação por IA (requer LM Studio em execução)?",
                  font=("Helvetica", 10)).pack(pady=(10, 5))
        
        self.ia_var = tk.BooleanVar(value=True)
        
        if not IA_DISPONIVEL:
            ttk.Radiobutton(dialog, text="Não (modo manual)", variable=self.ia_var, value=False, state="disabled").pack()
            ttk.Label(dialog, text="  (openai não instalado — a IA está indisponível)", 
                      font=("Helvetica", 8), foreground="red").pack()
        else:
            ttk.Radiobutton(dialog, text="Sim (avaliação com IA)", variable=self.ia_var, value=True).pack(pady=2)
            ttk.Radiobutton(dialog, text="Não (modo manual — apenas perguntas aleatórias)", 
                           variable=self.ia_var, value=False).pack(pady=2)
        
        ttk.Label(dialog, text="O modo manual seleciona perguntas aleatoriamente sem avaliação.",
                  font=("Helvetica", 8), foreground="#888888").pack(pady=(5, 15))
        
        def confirmar():
            self.ia_ativada = self.ia_var.get()
            dialog.destroy()
        
        ttk.Button(dialog, text="Confirmar", command=confirmar).pack(pady=(5, 15))
        
        # Focar na janela
        dialog.focus_set()
        self.root.wait_window(dialog)

    def carregar_perguntas_estruturadas(self, caminho_ficheiro):
        """Lê o ficheiro markdown e extrai as perguntas associando-as à respetiva semana."""
        perguntas_estruturadas = []
        semana_atual = "Desconhecida"
        
        try:
            with open(caminho_ficheiro, 'r', encoding='utf-8') as f:
                linhas = f.readlines()
                
            for linha in linhas:
                linha_str = linha.strip()
                
                if linha_str.startswith("### "):
                    semana_atual = linha_str.replace("### ", "")
                    continue
                    
                match_pergunta = re.match(r'^(\d+)\.\s+(.+)', linha_str)
                if match_pergunta:
                    num_pergunta = match_pergunta.group(1)
                    texto_pergunta = match_pergunta.group(2)
                    
                    perguntas_estruturadas.append({
                        "semana": semana_atual,
                        "numero": num_pergunta,
                        "texto": texto_pergunta
                    })
                    
            return perguntas_estruturadas
        except FileNotFoundError:
            return []

    def configurar_estilos_dark(self):
        """Configura o tema escuro (Dark Mode) global para os widgets do ttk."""
        self.root.configure(bg="#1e1e1e")
        
        style = ttk.Style()
        style.theme_use('clam')
        
        style.configure(".", background="#1e1e1e", foreground="#ffffff")
        style.configure("TFrame", background="#1e1e1e")
        style.configure("TLabel", background="#1e1e1e", foreground="#ffffff")
        
        style.configure("TButton", background="#3a3a3a", foreground="#ffffff", borderwidth=1, focuscolor="none")
        style.map("TButton",
                  background=[('active', '#4a4a4a'), ('pressed', '#2b2b2b')],
                  foreground=[('active', '#ffffff')])
                  
        style.configure("TScale", background="#1e1e1e", troughcolor="#333333")
        style.configure("TRadiobutton", background="#1e1e1e", foreground="#ffffff")

    def criar_interface(self):
        main_frame = ttk.Frame(self.root, padding="15")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # Cabeçalho da Pergunta
        self.lbl_cabecalho = ttk.Label(main_frame, text="SEMANA X | PERGUNTA Nº Y", font=("Helvetica", 11, "bold"), foreground="#4da3ff")
        self.lbl_cabecalho.pack(anchor=tk.W, pady=(0, 5))
        
        # Enunciado da Pergunta
        self.lbl_pergunta = tk.Message(main_frame, text="", font=("Helvetica", 11), width=1050, anchor="w", justify="left", bg="#1e1e1e", fg="#e0e0e0")
        self.lbl_pergunta.pack(fill=tk.X, pady=(0, 10))
        
        # Área de Resposta do Utilizador
        ttk.Label(main_frame, text="A tua resposta (máx. 2 minutos de escrita):", font=("Helvetica", 10, "bold"), foreground="#aaaaaa").pack(anchor=tk.W)
        self.txt_resposta = ScrolledText(main_frame, height=5, font=("Consolas", 11), wrap=tk.WORD, bg="#2d2d2d", fg="#ffffff", insertbackground="white", relief=tk.FLAT)
        self.txt_resposta.pack(fill=tk.BOTH, expand=False, pady=(5, 10))
        
        # Painel de Controlos (Botões e Slider)
        control_frame = ttk.Frame(main_frame)
        control_frame.pack(fill=tk.X, pady=(0, 10))
        
        self.btn_submeter = ttk.Button(control_frame, text="Submeter Resposta para Avaliação", command=self.submeter_resposta)
        self.btn_submeter.pack(side=tk.LEFT, padx=(0, 10), ipady=4)
        
        self.btn_proxima = ttk.Button(control_frame, text="Saltar / Próxima Pergunta", command=self.proxima_pergunta)
        self.btn_proxima.pack(side=tk.LEFT, padx=(0, 20), ipady=4)
        
        # Slider de ajuste de tamanho de fonte
        ttk.Label(control_frame, text="Tamanho do Texto IA:", font=("Helvetica", 9), foreground="#aaaaaa").pack(side=tk.LEFT, padx=(10, 5))
        self.slider_fonte = ttk.Scale(control_frame, from_=10, to=22, value=13, orient=tk.HORIZONTAL, length=180, command=self.disparar_redimensionamento)
        self.slider_fonte.pack(side=tk.LEFT, fill=tk.X, expand=False)
        
        # Área de Avaliação
        self.lbl_avaliacao = ttk.Label(main_frame, text="Avaliação Formatada da IA:", font=("Helvetica", 10, "bold"), foreground="#2ecc71")
        self.lbl_avaliacao.pack(anchor=tk.W, pady=(5, 0))
        
        # Inicializa o componente HTMLText
        self.txt_avaliacao = HTMLText(main_frame, html="<div></div>")
        self.txt_avaliacao.configure(bg="#2d2d2d", relief=tk.FLAT)
        self.txt_avaliacao.pack(fill=tk.BOTH, expand=True, pady=(5, 0))
        
        # Renderização inicial
        self.renderizar_com_tamanho_atual()
        
        # Atualizar botão se IA não estiver disponível
        if not IA_DISPONIVEL:
            self.btn_submeter.config(state="disabled")
            self.lbl_avaliacao.config(text="IA indisponível (openai não instalado):", foreground="#ff7675")

    def disparar_redimensionamento(self, event=None):
        """Atualiza a renderização quando o Slider é movido."""
        self.renderizar_com_tamanho_atual()

    def renderizar_com_tamanho_atual(self):
        """Compila o Markdown injetando estilos diretamente via Inline."""
        tamanho_base = int(float(self.slider_fonte.get()))
        tamanho_titulo = tamanho_base + 5
        tamanho_codigo = tamanho_base - 1
        
        html_puro = markdown.markdown(self.ultimo_markdown_ia)
        
        html_puro = html_puro.replace("<h1>", f"<h1 style='font-size:{tamanho_titulo}px; color:#4da3ff; font-family:sans-serif; margin-top:15px; margin-bottom:8px; border-bottom:1px solid #444; padding-bottom:4px;'>")
        html_puro = html_puro.replace("<h2>", f"<h2 style='font-size:{tamanho_base+2}px; color:#17a2b8; font-family:sans-serif;'>")
        html_puro = html_puro.replace("<p>", f"<p style='font-size:{tamanho_base}px; font-family:sans-serif; color:#e0e0e0; line-height:1.4;'>")
        html_puro = html_puro.replace("<li>", f"<li style='font-size:{tamanho_base}px; font-family:sans-serif; color:#e0e0e0; margin-bottom:4px;'>")
        html_puro = html_puro.replace("<ul>", f"<ul style='margin-left: 20px;'>")
        
        html_puro = html_puro.replace("<pre><code>", f"<pre style='background-color:#1e1e1e; border-left:4px solid #2ecc71; padding:12px; margin:10px 0; font-family:Consolas,monospace; font-size:{tamanho_codigo}px; color:#a29bfe;'>")
        html_puro = html_puro.replace("<code>", f"<code style='background-color:#1e1e1e; color:#ff7675; font-family:Consolas,monospace; font-size:{tamanho_codigo}px; padding:2px 4px;'>")
        
        html_final = f"<div style='background-color:#2d2d2d; color:#e0e0e0; padding:8px; font-family:sans-serif;'>{html_puro}</div>"
        
        self.txt_avaliacao.set_html(html_final)

    def proxima_pergunta(self):
        self.pergunta_atual = random.choice(self.perguntas)
        self.pergunta_numero += 1
        self.lbl_cabecalho.config(text=f"PERGUNTA {self.pergunta_numero}/{len(self.perguntas)} | {self.pergunta_atual['semana'].upper()} | Nº {self.pergunta_atual['numero']}")
        self.lbl_pergunta.config(text=self.pergunta_atual['texto'])
        
        self.txt_resposta.delete("1.0", tk.END)
        self.ultimo_markdown_ia = "# Instruções\nEscreve a tua resposta acima e submete para obter o teu feedback."
        self.renderizar_com_tamanho_atual()
        self.txt_resposta.focus_set()

    def submeter_resposta(self):
        resposta_user = self.txt_resposta.get("1.0", tk.END).strip()
        
        if not resposta_user:
            messagebox.showwarning("Aviso", "Por favor, escreve alguma resposta antes de submeter.")
            return
        
        # Se a IA não está ativada, mostrar mensagem
        if not self.ia_ativada:
            self.ultimo_markdown_ia = (
                f"# Resposta Submetida\n\n"
                f"**Pergunta:** {self.pergunta_atual['semana']} - Nº {self.pergunta_atual['numero']}\n\n"
                f"## A tua resposta:\n\n"
                f"```\n"
                f"{resposta_user}\n"
                f"```\n\n"
                f"> *Modo manual ativado — sem avaliação por IA.*\n\n"
                f"---\n\n"
                f"**Dica:** Pensa nos pontos-chave que deves incluir numa resposta de defesa:\n"
                f"- Conceitos teóricos relevantes\n"
                f"- Referências ao código do projeto\n"
                f"- Explicação clara e estruturada\n"
                f"- Exemplos práticos quando aplicável"
            )
            self.renderizar_com_tamanho_atual()
            return
        
        self.txt_avaliacao.set_html("<div style='font-family: sans-serif; color: #4da3ff; padding: 10px;'><b>A enviar dados e a aguardar feedback estruturado do modelo local (qwen3.6-35b-a3b)... Por favor, espera.</b></div>")
        self.root.update_idletasks()
        
        prompt_sistema = (
            "Atues como um professor universitário exigente de Engenharia de Software.\n"
            "O utilizador vai responder a uma pergunta teórica. Deves focar-te no rigor técnico e conceitos chave.\n"
            "IMPORTANTE: Considera que o utilizador teve menos de 2 minutos para responder. Não penalizes a falta de extensão. Na resposta corrigida, considera que o utilizador tem de a poder escrever em menos de 2 minutos.\n\n"
            "Estrutura a tua resposta obrigatoriamente com a seguinte hierarquia Markdown (Usa apenas cardinal simple '#' para os títulos principais):\n"
            "# 1. NOTA\n"
            "[Pontuação de 0 a 100]\n\n"
            "# 2. O QUE FALTOU / PONTOS DE MELHORIA\n"
            "[Teu feedback estruturado aqui]\n\n"
            "# 3. PROPOSTA DE RESPOSTA CORRIGIDA\n"
            "```\n"
            "[Tua proposta aqui]\n"
            "```"
        )
        
        prompt_utilizador = (
            f"CONTEXTO: {self.pergunta_atual['semana']} - Pergunta nº {self.pergunta_atual['numero']}\n"
            f"PERGUNTA: {self.pergunta_atual['texto']}\n\n"
            f"RESPOSTA DO UTILIZADOR: {resposta_user}"
        )
        
        try:
            completion = client.chat.completions.create(
                model=MODELO_LM_STUDIO,
                messages=[
                    {"role": "system", "content": prompt_sistema},
                    {"role": "user", "content": prompt_utilizador}
                ],
                temperature=0.2,
            )
            resultado_cru = completion.choices[0].message.content
            
            self.ultimo_markdown_ia = re.sub(r'^``\s*', '', resultado_cru)
            self.ultimo_markdown_ia = re.sub(r'``\s*$', '', self.ultimo_markdown_ia)
            
            self.renderizar_com_tamanho_atual()
            
        except Exception as e:
            self.txt_avaliacao.set_html(f"<div style='color:#ff7675; font-family: sans-serif; padding:10px;'><b>Erro ao ligar ao LM Studio:</b><br>{e}</div>")

if __name__ == "__main__":
    root = tk.Tk()
    app = AplicacaoEstudos(root)
    root.mainloop()
