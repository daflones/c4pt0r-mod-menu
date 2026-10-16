# C4pt0r Cheats — FiveM Internal

Cheat interno para FiveM com injetor GUI e menu ImGui.

**Features:** ESP, Aimbot, Godmode, Invisível, Speed Hack, No Recoil, Noclip, Teleport (Waypoint/Salvo).

---

## Estrutura do Projeto

```
C4pt0r-Cheats/
├── injector/
│   └── main.cpp            ← Injetor Win32 GUI (dark theme)
├── cheat/
│   ├── dllmain.cpp          ← Entry point da DLL
│   ├── globals.h            ← Configurações e estado global
│   ├── memory.h             ← Pattern scanner, read/write
│   ├── natives.h            ← Hashes e wrappers de natives
│   ├── natives.cpp          ← Implementação do sistema de natives
│   ├── hooks.h / hooks.cpp  ← Hook D3D11 Present + WndProc
│   ├── menu.h / menu.cpp    ← Menu ImGui (tabs, style)
│   ├── features.h           ← Interface das features
│   └── features.cpp         ← ESP, Aimbot, Godmode, etc.
└── README.md
```

---

## Dependências

### 1. Visual Studio 2022
- Instale com o workload **"Desenvolvimento para desktop com C++"**
- Windows SDK 10+ (vem junto)

### 2. Dear ImGui
Baixe do GitHub: https://github.com/ocornut/imgui

Copie estes arquivos para `cheat/deps/imgui/`:
```
imgui.h
imgui.cpp
imgui_draw.cpp
imgui_tables.cpp
imgui_widgets.cpp
imgui_internal.h
imconfig.h
imstb_rectpack.h
imstb_textedit.h
imstb_truetype.h
backends/imgui_impl_dx11.h
backends/imgui_impl_dx11.cpp
backends/imgui_impl_win32.h
backends/imgui_impl_win32.cpp
```

---

## Como Compilar

### Projeto 1: Injetor (EXE)

1. **Visual Studio** → `File > New > Project > Empty Project`
2. Nome: `C4pt0r-Injector`
3. Adicione `injector/main.cpp` ao projeto
4. **Project Properties:**
   - Configuration: `Release`, Platform: `x64`
   - `Linker > System > SubSystem`: **Windows (/SUBSYSTEM:WINDOWS)**
   - `C/C++ > Language > C++ Language Standard`: **C++17**
   - `Linker > Input > Additional Dependencies`: Adicione `dwmapi.lib;shell32.lib;comctl32.lib;comdlg32.lib`
5. Build (Ctrl+Shift+B)

### Projeto 2: Cheat DLL

1. **Visual Studio** → `File > New > Project > Empty Project` (ou Dynamic-Link Library)
2. Nome: `C4pt0r-Cheat`
3. Adicione todos os arquivos de `cheat/` ao projeto
4. Adicione os arquivos ImGui do `cheat/deps/imgui/` ao projeto
5. **Project Properties:**
   - Configuration: `Release`, Platform: `x64`
   - `General > Configuration Type`: **Dynamic Library (.dll)**
   - `C/C++ > Language > C++ Language Standard`: **C++17**
   - `C/C++ > General > Additional Include Directories`: Adicione `deps/imgui` e `deps/imgui/backends`
   - `Linker > Input > Additional Dependencies`: Adicione `d3d11.lib;dxgi.lib;psapi.lib`
6. Build (Ctrl+Shift+B)

---

## Como Usar

### Passo a Passo

1. **Compile** o injetor (`C4pt0r-Injector.exe`) e a DLL (`C4pt0r-Cheat.dll`)
2. Coloque ambos na mesma pasta (o injetor auto-detecta a DLL pelo nome)
3. **Abra o FiveM** e entre em um servidor
4. **Espere carregar** completamente (spawn no mapa)
5. **Execute o injetor como Administrador** (botão direito → Executar como admin)
6. Clique **INJETAR**
7. No jogo, aperte **F9** para abrir/fechar o menu

### Teclas

| Tecla | Ação |
|-------|------|
| **F9** | Abre/fecha o menu |
| **END** | Descarrega o cheat (panic key) |
| **Botão direito** | Ativa aimbot (enquanto segura) |

---

## Features

### Combat
- **Aimbot** — Mira automática com FOV ajustável, smoothing, seleção de osso (cabeça/pescoço/peito), filtro de visibilidade
- **No Recoil** — Cancela o recuo salvando/restaurando o pitch da câmera

### Visuals
- **ESP** — Caixas corner-style, nomes, barras de vida, snaplines, distância. Cores customizáveis, distância máxima ajustável

### Player
- **Godmode** — Invencível + vida sempre cheia
- **Invisível** — Outros jogadores não te veem
- **Speed Hack** — Multiplicador de velocidade (corrida/sprint)
- **Noclip** — Voa através de paredes (WASD + Space/Ctrl)

### Teleport
- **TP para Waypoint** — Marca no mapa e teleporta
- **Salvar/Carregar posição** — Salva sua posição atual e teleporta de volta

---

## Notas Técnicas

### Sistema de Natives
O cheat encontra a função `GetNativeHandler` do FiveM via pattern scanning (AOB).
Isso mapeia hashes de 64 bits para funções que executam os natives do GTA V.

**Se o pattern não for encontrado** (FiveM atualizou), você precisa:
1. Abrir o FiveM no x64dbg ou IDA
2. Encontrar o novo pattern para `rage::scrEngine::GetNativeHandler`
3. Atualizar os patterns em `natives.cpp` → array `patterns[]`

### Hook D3D11
Usa VTable swap no `IDXGISwapChain::Present` (índice 8). Cria um device/swapchain dummy para pegar o endereço da VTable, depois substitui o ponteiro.

### Thread Safety
- Features rodam na thread principal do cheat (~60 fps tick)
- ESP data é coletada no tick e guardada num buffer com mutex
- Rendering do ESP lê o buffer no hook do Present (thread do render)

---

## Troubleshooting

| Problema | Solução |
|----------|---------|
| "FiveM não encontrado" | Certifique que o FiveM está rodando E você já está dentro de um servidor (processo GTAProcess precisa existir) |
| "OpenProcess falhou" | Execute o injetor como **Administrador** |
| Menu não aparece | O D3D11 hook pode ter falhado — verifique DebugView para logs `[C4pt0r]` |
| Game crash | Pattern de natives pode estar desatualizado — veja Notas Técnicas |
| Aimbot não funciona | Segure o botão direito do mouse. Verifique se FOV não está muito baixo |

---

## Atualizando Patterns

Quando o FiveM atualiza, os patterns de memória podem mudar. Use:

- **x64dbg** — Para encontrar funções e patterns
- **Ghidra / IDA** — Para análise estática
- **Cheat Engine** — Para buscar valores e estruturas

O arquivo principal para atualizar é `cheat/natives.cpp`, array `patterns[]`.

---

*C4pt0r Cheats v1.0 — by Larpbase*
