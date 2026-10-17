/** UI-side view of the latest job seen on WS / job status (not a wire type). */
export interface ActiveJob {
  jobId: string;
  state: string;
  fraction: number;
  stage: string;
  error?: string;
}

/** D2: workflow stages, left→right. Catalog insert lives only on 'scene'. */
export type WorkflowStage =
  | 'editor'
  | 'scene'
  | 'objects'
  | 'engines'
  | 'run'
  | 'results';

export const WORKFLOW_STAGES: ReadonlyArray<{
  id: WorkflowStage;
  label: string;
}> = [
  { id: 'editor', label: 'Editor' },
  { id: 'scene', label: 'Scene' },
  { id: 'objects', label: 'Objects / Sources' },
  { id: 'engines', label: 'Engines' },
  { id: 'run', label: 'Run' },
  { id: 'results', label: 'Results' },
];
