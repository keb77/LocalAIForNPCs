# LocalAIForNPCs

An Unreal Engine plugin that brings fully **local AI-driven interactions** to your NPCs.  
Includes **ASR**, **LLM (with optional RAG)**, **TTS**, and **LipSync** capabilities, all exposed through a simple, modular component system.

---

## Architecture Overview

The plugin provides **three core components** (one per subsystem) and **two high-level components** that manage interactions.

### Core Components

#### **ASRComponent**
- Records speech from the microphone  
- Optional **Voice Activity Detection (VAD)** for automatic speech segmentation (no push-to-talk required)  
- Generates transcriptions via **whisper.cpp**

#### **LLMComponent**
- Performs LLM inference using **llama.cpp**  
- Supports **Retrieval Augmented Generation (RAG)** using embedding and (optional) reranker models  
- Includes an **Action System**, enabling the LLM to output actions alongside textual responses  

#### **TTSComponent**
- Generates speech audio via **Kokoro-FastAPI**  
- Produces lip-sync animation using **NeuroSync** or **Audio2Face**  
- Handles audio + animation playback

### High-Level Components

#### **NPCComponent**
Attach to any NPC Actor.  
Contains ASRComponent, LLMComponent, and TTSComponent and manages the full conversational pipeline: **listen → think → speak → animate**

#### **PlayerComponent**
Attach to the player Pawn.  
Handles sending player audio/text to NPCs and receiving/playing NPC responses.

---

## External Dependencies

These systems rely on external local servers. Start only the ones you need.

### ASR — whisper.cpp server
- Follow setup instructions:  
  https://github.com/ggml-org/whisper.cpp  
- Download models:  
  https://huggingface.co/ggerganov/whisper.cpp/tree/main  
- Start the server using `whisper-server` (recommended port: 8000)

### LLM — llama.cpp server
- Follow setup instructions:
  https://github.com/ggml-org/llama.cpp  
- Download compatible chat models (e.g. from Hugging Face):  
  https://huggingface.co/  
- Start the server using `llama-server`
- For **RAG**, start additional llama.cpp servers:
  - Embedding model: `--embedding` (recommended port: 8081)  
  - Reranker model (optional): `--reranking` (recommended port: 8082)

### TTS — Kokoro-FastAPI server
- Follow setup instructions:
  https://github.com/remsky/Kokoro-FastAPI  
- Start the server using `start-cpu` or `start-gpu`
**Offline usage tip:**  
For reliable offline startup, remove these lines from the startup script (after starting it the first time):
<pre> uv pip install -e ".[cpu]" / ".[gpu]"
uv run --no-sync python docker/scripts/download_model.py --output api/src/models/v1_0 </pre>

### LipSync — NeuroSync or Audio2Face (recommended to use with MetaHumans)

#### **NeuroSync**
- Run `Source/ThirdParty/NeuroSync/generate_executable.bat`
- Follow setup instructions: 
  https://github.com/AnimaVR/NeuroSync_Local_API
- Start the server using `neurosync_local_api.py` (recommended: change port in script to 8881)

#### **Audio2Face**
- Download plugins and models:  
  https://developer.nvidia.com/ace-for-games  
- Configure NPCs following NVIDIA’s documentation:  
  https://docs.nvidia.com/ace/ace-unreal-plugin/2.5/index.html

---

## Getting Started

1. **Install & Enable the Plugin**  
   Copy the plugin into your project's `Plugins/` folder and enable it in **Edit → Plugins**.  
   Restart the editor if prompted.

2. **Start the Required Servers**

3. **Add Components to Actors**
   - **Recommended setup:**  
     - Add **NPCComponent** to NPC Actor(s). If NPC is a MetaHuman, attach it to its Face, enable Generate Overlap Events, and set Collision Preset to Pawn.
     - Add **PlayerComponent** to the player Pawn.
   - **Partial / custom setups:**  
     Add **ASRComponent**, **LLMComponent**, or **TTSComponent** individually if you only need part of the pipeline.  
     Note that **PlayerComponent** only integrates with NPCComponent, otherwise interaction must be custom-handled.

4. **Configure Component Properties**

