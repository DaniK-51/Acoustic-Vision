/** UI-side view of the latest job seen on WS / job status (not a wire type). */
export interface ActiveJob {
  jobId: string;
  state: string;
  fraction: number;
  stage: string;
  error?: string;
}
