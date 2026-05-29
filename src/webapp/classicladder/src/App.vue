<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import ComParamsPanel from './components/ComParamsPanel.vue';
import RequestsPanel from './components/RequestsPanel.vue';
import StatusPanel from './components/StatusPanel.vue';
import { modbusStore } from './stores/modbus';

type Tab = 'com-params' | 'requests' | 'status';
const activeTab = ref<Tab>('com-params');

onMounted(() => {
  modbusStore.fetchAll();
  modbusStore.startPolling();
});

onUnmounted(() => {
  modbusStore.stopPolling();
});
</script>

<template>
  <div class="app">
    <header class="header">
      <h1>Classic Ladder — Modbus Configuration</h1>
      <div class="status-indicator" v-if="modbusStore.state.status">
        <span :class="modbusStore.state.status.running ? 'dot-green' : 'dot-red'"></span>
        {{ modbusStore.state.status.running ? 'Running' : 'Stopped' }}
      </div>
    </header>

    <nav class="tabs">
      <button :class="{ active: activeTab === 'com-params' }" @click="activeTab = 'com-params'">
        COM Parameters
      </button>
      <button :class="{ active: activeTab === 'requests' }" @click="activeTab = 'requests'">
        I/O Requests
      </button>
      <button :class="{ active: activeTab === 'status' }" @click="activeTab = 'status'">
        Status
      </button>
    </nav>

    <div class="error-bar" v-if="modbusStore.state.error">
      {{ modbusStore.state.error }}
      <button @click="modbusStore.state.error = ''">✕</button>
    </div>

    <main class="content">
      <ComParamsPanel v-if="activeTab === 'com-params'" />
      <RequestsPanel v-if="activeTab === 'requests'" />
      <StatusPanel v-if="activeTab === 'status'" />
    </main>
  </div>
</template>

<style>
* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
  font-size: 14px;
  background: #1e1e2e;
  color: #cdd6f4;
}

.app {
  max-width: 900px;
  margin: 0 auto;
  padding: 16px;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.header h1 {
  font-size: 18px;
  font-weight: 600;
}

.status-indicator {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
}

.dot-green, .dot-red {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}
.dot-green { background: #a6e3a1; }
.dot-red { background: #f38ba8; }

.tabs {
  display: flex;
  gap: 2px;
  margin-bottom: 16px;
  border-bottom: 1px solid #45475a;
}

.tabs button {
  background: none;
  border: none;
  color: #a6adc8;
  padding: 8px 16px;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  font-size: 13px;
}

.tabs button.active {
  color: #cdd6f4;
  border-bottom-color: #89b4fa;
}

.tabs button:hover {
  color: #cdd6f4;
}

.error-bar {
  background: #f38ba8;
  color: #1e1e2e;
  padding: 8px 12px;
  border-radius: 4px;
  margin-bottom: 12px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.error-bar button {
  background: none;
  border: none;
  font-size: 16px;
  cursor: pointer;
  color: #1e1e2e;
}

.content {
  background: #313244;
  border-radius: 8px;
  padding: 16px;
}

/* Form elements */
label {
  display: block;
  font-size: 12px;
  color: #a6adc8;
  margin-bottom: 4px;
}

input, select {
  background: #1e1e2e;
  border: 1px solid #45475a;
  color: #cdd6f4;
  padding: 6px 10px;
  border-radius: 4px;
  font-size: 13px;
  width: 100%;
}

input:focus, select:focus {
  outline: none;
  border-color: #89b4fa;
}

button.btn {
  background: #89b4fa;
  color: #1e1e2e;
  border: none;
  padding: 8px 16px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  font-weight: 500;
}

button.btn:hover {
  background: #b4d0fb;
}

button.btn-danger {
  background: #f38ba8;
}

button.btn-danger:hover {
  background: #f5a3b8;
}
</style>
