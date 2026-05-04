<script setup lang="ts">
import { ref } from 'vue';
import { halshowStore } from '../stores/halshow';

const editingName = ref('');
const editValue = ref('');
const editError = ref('');

function getWatchValue(name: string): string {
  const item = halshowStore.state.watchValues.find(v => v.name === name);
  return item?.value ?? '—';
}

function getWatchType(name: string): string {
  const item = halshowStore.state.watchValues.find(v => v.name === name);
  return item?.type ?? '';
}

function getWatchDir(name: string): string {
  const item = halshowStore.state.watchValues.find(v => v.name === name);
  return item?.dir ?? '';
}

function startEdit(name: string) {
  editingName.value = name;
  editValue.value = getWatchValue(name);
  editError.value = '';
}

async function submitEdit() {
  if (!editingName.value) return;
  const result = await halshowStore.setWatchValue(editingName.value, editValue.value);
  if (result.success) {
    editingName.value = '';
    editError.value = '';
  } else {
    editError.value = result.error ?? 'Failed';
  }
}

function cancelEdit() {
  editingName.value = '';
  editError.value = '';
}
</script>

<template>
  <div class="watch-panel">
    <div class="watch-header">
      <span>Watch List ({{ halshowStore.state.watchList.length }} items)</span>
      <button v-if="halshowStore.state.watchList.length > 0" @click="halshowStore.clearWatch()">Clear All</button>
    </div>

    <div v-if="halshowStore.state.watchList.length === 0" class="empty">
      No items being watched.
      <br /><br />
      Double-click items in the tree or click "+ Watch" in the Show tab to add.
    </div>

    <table v-else class="watch-table">
      <thead>
        <tr>
          <th>Name</th>
          <th>Value</th>
          <th>Type</th>
          <th>Dir</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="name in halshowStore.state.watchList" :key="name">
          <td class="name">{{ name }}</td>
          <td class="value" @dblclick="startEdit(name)">
            <template v-if="editingName === name">
              <input
                v-model="editValue"
                class="edit-input"
                @keydown.enter="submitEdit"
                @keydown.escape="cancelEdit"
                @blur="cancelEdit"
                autofocus
              />
              <span v-if="editError" class="edit-error">{{ editError }}</span>
            </template>
            <template v-else>{{ getWatchValue(name) }}</template>
          </td>
          <td class="type">{{ getWatchType(name) }}</td>
          <td class="dir">{{ getWatchDir(name) }}</td>
          <td class="remove">
            <button @click="halshowStore.removeFromWatch(name)">×</button>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
</template>

<style scoped>
.watch-panel {
  font-size: 12px;
}

.watch-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
  padding-bottom: 4px;
  border-bottom: 1px solid #333;
  color: #4af;
  font-size: 13px;
  font-weight: 600;
}

.watch-header button {
  background: #222;
  color: #999;
  border: 1px solid #444;
  border-radius: 3px;
  padding: 2px 8px;
  font-size: 11px;
  cursor: pointer;
}

.watch-header button:hover {
  background: #3a1a1a;
  color: #f88;
  border-color: #844;
}

.empty {
  color: #666;
  font-style: italic;
  padding: 12px 0;
}

.watch-table {
  width: 100%;
  border-collapse: collapse;
}

.watch-table th {
  text-align: left;
  color: #888;
  padding: 4px 6px;
  border-bottom: 1px solid #333;
  font-weight: normal;
}

.watch-table td {
  padding: 3px 6px;
  border-bottom: 1px solid #222;
}

.watch-table .name {
  font-family: monospace;
  color: #ccc;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.watch-table .value {
  font-family: monospace;
  color: #4f4;
  font-weight: 600;
  cursor: text;
}

.watch-table .value .edit-input {
  background: #222;
  border: 1px solid #4a8abf;
  border-radius: 2px;
  padding: 1px 4px;
  color: #fff;
  font-family: monospace;
  font-size: 12px;
  width: 100px;
}

.watch-table .value .edit-error {
  color: #f66;
  font-size: 10px;
  margin-left: 4px;
}

.watch-table .dir {
  color: #888;
}

.watch-table .type {
  color: #888;
}

.watch-table .remove button {
  background: none;
  border: none;
  color: #666;
  cursor: pointer;
  font-size: 14px;
  padding: 0 4px;
}

.watch-table .remove button:hover {
  color: #f44;
}
</style>
