<script setup lang="ts">
import { computed } from 'vue';
import { scopeStore } from '../stores/scope';
import { ScopeState, TrigEdge } from '../generated/halscope_client';

const stateLabel = computed(() => {
  const labels: Record<number, string> = {
    [ScopeState.IDLE]: 'Idle',
    [ScopeState.INIT]: 'Init',
    [ScopeState.PRE_TRIG]: 'Pre-trigger',
    [ScopeState.TRIG_WAIT]: 'Waiting',
    [ScopeState.POST_TRIG]: 'Capturing',
    [ScopeState.DONE]: 'Done',
    [ScopeState.RESET]: 'Reset',
  };
  return labels[scopeStore.state.status.state] ?? 'Unknown';
});

const stateClass = computed(() => {
  const s = scopeStore.state.status.state;
  if (s === ScopeState.DONE) return 'state-done';
  if (s === ScopeState.TRIG_WAIT || s === ScopeState.PRE_TRIG) return 'state-waiting';
  if (s === ScopeState.POST_TRIG) return 'state-capturing';
  return '';
});

const isRunning = computed(() => {
  const s = scopeStore.state.status.state;
  return s === ScopeState.INIT || s === ScopeState.PRE_TRIG ||
    s === ScopeState.TRIG_WAIT || s === ScopeState.POST_TRIG;
});

const canArm = computed(() => {
  const s = scopeStore.state.status.state;
  return (s === ScopeState.IDLE || s === ScopeState.DONE) && scopeStore.state.connected;
});

const trigChannels = computed(() =>
  scopeStore.state.status.channels.filter(c => c.enabled)
);

const sampleProgress = computed(() => {
  const st = scopeStore.state.status;
  if (st.recLen === 0) return 0;
  return Math.round((st.samples / st.recLen) * 100);
});

function onRun() {
  scopeStore.setAutoRearm(true);
  scopeStore.arm();
}

function onStop() {
  scopeStore.stop();
}

function onSingle() {
  scopeStore.setAutoRearm(false);
  scopeStore.arm();
}

async function onApplyConfig() {
  await scopeStore.configure();
  await scopeStore.setTrigger();
}
</script>

<template>
  <div class="toolbar">
    <!-- Connection -->
    <div class="toolbar-group">
      <button
        v-if="!scopeStore.state.connected"
        class="btn btn-connect"
        @click="scopeStore.connect()"
      >
        Connect
      </button>
      <span v-else class="connected-badge">● Connected</span>
    </div>

    <!-- State indicator -->
    <div class="toolbar-group">
      <span class="state-badge" :class="stateClass">{{ stateLabel }}</span>
      <div v-if="isRunning" class="progress-bar">
        <div class="progress-fill" :style="{ width: sampleProgress + '%' }"></div>
      </div>
    </div>

    <!-- Controls -->
    <div class="toolbar-group">
      <button class="btn btn-run" @click="onRun" :disabled="!canArm">▶ Run</button>
      <button class="btn" @click="onSingle" :disabled="!canArm">⎍ Single</button>
      <button class="btn btn-stop" @click="onStop" :disabled="!isRunning">■ Stop</button>
    </div>

    <!-- Capture config -->
    <div class="toolbar-group config-group">
      <label>
        Thread
        <select
          :value="scopeStore.state.selectedThread"
          @change="scopeStore.setSelectedThread(($event.target as HTMLSelectElement).value)"
        >
          <option v-for="t in scopeStore.state.threads" :key="t.name" :value="t.name">
            {{ t.name }} ({{ (t.periodNs / 1000).toFixed(0) }}µs)
          </option>
        </select>
      </label>
      <label>
        Rec Len
        <input type="number" v-model.number="scopeStore.captureConfig.recLen" min="100" max="65536" step="100" />
      </label>
      <label>
        Mult
        <input type="number" v-model.number="scopeStore.captureConfig.samplePeriodMult" min="1" max="1000" />
      </label>
      <label>
        Pre-trig
        <input type="number" v-model.number="scopeStore.captureConfig.preTrig" min="0" />
      </label>
    </div>

    <!-- Trigger config -->
    <div class="toolbar-group config-group">
      <label>
        Trig Ch
        <select v-model.number="scopeStore.triggerConfig.channel">
          <option v-for="ch in trigChannels" :key="ch.channel" :value="ch.channel">
            {{ ch.pinName || `Ch ${ch.channel}` }}
          </option>
        </select>
      </label>
      <label>
        Level
        <input type="number" v-model.number="scopeStore.triggerConfig.level" step="0.1" />
      </label>
      <label>
        Edge
        <select v-model.number="scopeStore.triggerConfig.edge">
          <option :value="TrigEdge.RISING">Rising</option>
          <option :value="TrigEdge.FALLING">Falling</option>
        </select>
      </label>
      <label class="checkbox-label">
        <input type="checkbox" v-model="scopeStore.triggerConfig.autoTrig" />
        Auto
      </label>
      <label class="checkbox-label">
        <input type="checkbox" v-model="scopeStore.triggerConfig.force" />
        Force
      </label>
      <button class="btn" @click="onApplyConfig">Apply</button>
    </div>

    <!-- Error -->
    <div v-if="scopeStore.state.error" class="error-bar">
      {{ scopeStore.state.error }}
    </div>
  </div>
</template>

<style scoped>
.toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  padding: 6px 8px;
  background: #1a1a1a;
  border-bottom: 1px solid #333;
  font-size: 12px;
}

.toolbar-group {
  display: flex;
  align-items: center;
  gap: 6px;
}

.config-group label {
  display: flex;
  align-items: center;
  gap: 3px;
  color: #999;
}

.config-group input[type="number"] {
  width: 60px;
  background: #222;
  border: 1px solid #444;
  border-radius: 3px;
  color: #ccc;
  padding: 2px 4px;
  font-size: 11px;
}

.config-group select {
  background: #222;
  border: 1px solid #444;
  border-radius: 3px;
  color: #ccc;
  padding: 2px 4px;
  font-size: 11px;
  max-width: 140px;
}

.checkbox-label {
  cursor: pointer;
  user-select: none;
}

.btn {
  background: #333;
  color: #ccc;
  border: 1px solid #555;
  border-radius: 3px;
  cursor: pointer;
  padding: 3px 10px;
  font-size: 12px;
  white-space: nowrap;
}

.btn:hover { background: #444; }
.btn:disabled { opacity: 0.4; cursor: not-allowed; }

.btn-run { color: #4f4; border-color: #4a4; }
.btn-run:hover { background: #243; }
.btn-stop { color: #f44; border-color: #a44; }
.btn-stop:hover { background: #422; }
.btn-connect { color: #4af; border-color: #48a; }
.btn-connect:hover { background: #234; }

.connected-badge {
  color: #4f4;
  font-size: 12px;
}

.state-badge {
  padding: 2px 8px;
  border-radius: 3px;
  background: #333;
  font-weight: 600;
}

.state-done { color: #4f4; background: #1a2a1a; }
.state-waiting { color: #ff4; background: #2a2a1a; }
.state-capturing { color: #4af; background: #1a2a3a; }

.progress-bar {
  width: 60px;
  height: 6px;
  background: #333;
  border-radius: 3px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: #4af;
  transition: width 0.1s;
}

.error-bar {
  width: 100%;
  color: #f44;
  font-size: 11px;
  padding: 2px 4px;
}
</style>
