<script setup lang="ts">
import ScopeToolbar from './components/ScopeToolbar.vue';
import ScopeChart from './components/ScopeChart.vue';
import ChannelPanel from './components/ChannelPanel.vue';
import BufferIndicator from './components/BufferIndicator.vue';
</script>

<template>
  <div class="app">
    <ScopeToolbar />
    <div class="main-area">
      <div class="chart-area">
        <BufferIndicator />
        <ScopeChart />
      </div>
      <div class="side-panel">
        <ChannelPanel />
      </div>
    </div>
  </div>
</template>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

html, body, #app {
  width: 100%;
  height: 100%;
  background: #111;
  color: #ccc;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  font-size: 13px;
}

.app {
  display: flex;
  flex-direction: column;
  height: 100vh;
}

.main-area {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.chart-area {
  flex: 1;
  padding: 8px;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.side-panel {
  width: 280px;
  flex-shrink: 0;
  padding: 8px;
  border-left: 1px solid #333;
  overflow-y: auto;
}
</style>
