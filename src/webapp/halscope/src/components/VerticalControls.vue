<script setup lang="ts">
import { computed } from 'vue';
import { scopeStore } from '../stores/scope';

const selCh = computed(() => scopeStore.state.selectedChannel);
const hasSelection = computed(() => selCh.value >= 0);
const ui = computed(() => hasSelection.value ? scopeStore.channelUI[selCh.value] : null);
const color = computed(() => ui.value?.color ?? '#666');

// Scale presets: 1-2-5 sequence across a wide range
const SCALE_STEPS = [
  0.001, 0.002, 0.005,
  0.01, 0.02, 0.05,
  0.1, 0.2, 0.5,
  1, 2, 5,
  10, 20, 50,
  100, 200, 500,
  1000, 2000, 5000,
  10000, 20000, 50000,
];

const scaleIndex = computed({
  get() {
    if (!ui.value) return 9; // default = 1
    // Find closest step
    let best = 0;
    let bestDist = Infinity;
    for (let i = 0; i < SCALE_STEPS.length; i++) {
      const dist = Math.abs(Math.log(SCALE_STEPS[i]) - Math.log(ui.value.vScale));
      if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return best;
  },
  set(idx: number) {
    if (ui.value) {
      ui.value.vScale = SCALE_STEPS[idx];
    }
  },
});

const scaleLabel = computed(() => {
  if (!ui.value) return '---';
  const s = ui.value.vScale;
  if (s >= 1000) return (s / 1000).toFixed(0) + 'k/div';
  if (s >= 1) return s.toFixed(s >= 10 ? 0 : s >= 1 ? 1 : 2) + '/div';
  if (s >= 0.001) return (s * 1000).toFixed(0) + 'm/div';
  return s.toExponential(1) + '/div';
});

function onOffsetChange(e: Event) {
  if (ui.value) {
    ui.value.vOffset = Number((e.target as HTMLInputElement).value) / 100;
  }
}

function onAutoScale() {
  if (!ui.value || selCh.value < 0) return;
  const s = scopeStore.state.samples.find(s => s.channel === selCh.value);
  if (!s || s.data.length === 0) return;
  // Find data range
  let min = Infinity, max = -Infinity;
  for (let i = 0; i < s.data.length; i++) {
    if (s.data[i] < min) min = s.data[i];
    if (s.data[i] > max) max = s.data[i];
  }
  let range = max - min;
  if (range === 0) range = Math.abs(max) || 1;
  const target = range / 8;
  const exp = Math.floor(Math.log10(target));
  const base = Math.pow(10, exp);
  const norm = target / base;
  let scale: number;
  if (norm <= 1) scale = base;
  else if (norm <= 2) scale = 2 * base;
  else if (norm <= 5) scale = 5 * base;
  else scale = 10 * base;
  ui.value.vScale = scale || 1;
  const mid = (min + max) / 2;
  ui.value.vOffset = -(mid / ui.value.vScale);
  ui.value.scaleSet = true;
}
</script>

<template>
  <div class="vert-controls" :class="{ disabled: !hasSelection }">
    <div class="vert-header">
      <span class="vert-color" :style="{ background: color }"></span>
      <span class="vert-label">{{ hasSelection ? `Ch ${selCh}` : 'No channel' }}</span>
      <button v-if="hasSelection" class="btn-auto" @click="onAutoScale" title="Auto-fit scale">Auto</button>
    </div>
    <div class="vert-sliders">
      <label class="vert-slider-label">
        <span class="slider-name">Scale</span>
        <input
          type="range" :min="0" :max="SCALE_STEPS.length - 1" step="1"
          :value="scaleIndex"
          @input="scaleIndex = Number(($event.target as HTMLInputElement).value)"
          :disabled="!hasSelection"
          class="slider"
        />
        <span class="slider-value">{{ scaleLabel }}</span>
      </label>
      <label class="vert-slider-label">
        <span class="slider-name">Pos</span>
        <input
          type="range" min="-500" max="500" step="1"
          :value="Math.round((ui?.vOffset ?? 0) * 100)"
          @input="onOffsetChange"
          :disabled="!hasSelection"
          class="slider"
        />
        <span class="slider-value">{{ ui ? ui.vOffset.toFixed(1) : '---' }}</span>
      </label>
    </div>
  </div>
</template>

<style scoped>
.vert-controls {
  padding: 6px 0;
  border-top: 1px solid #333;
  border-bottom: 1px solid #333;
}

.vert-controls.disabled {
  opacity: 0.4;
}

.vert-header {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 4px;
  font-size: 12px;
}

.vert-color {
  width: 10px;
  height: 10px;
  border-radius: 2px;
  flex-shrink: 0;
}

.vert-label {
  font-weight: 600;
  flex: 1;
}

.btn-auto {
  background: #333;
  color: #ccc;
  border: 1px solid #555;
  border-radius: 3px;
  cursor: pointer;
  padding: 1px 6px;
  font-size: 10px;
}

.btn-auto:hover {
  background: #444;
}

.vert-sliders {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.vert-slider-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: #999;
}

.slider-name {
  width: 32px;
  flex-shrink: 0;
}

.slider {
  flex: 1;
  height: 16px;
  accent-color: #4af;
}

.slider-value {
  width: 55px;
  text-align: right;
  font-family: monospace;
  font-size: 10px;
  color: #aaa;
  flex-shrink: 0;
}
</style>
