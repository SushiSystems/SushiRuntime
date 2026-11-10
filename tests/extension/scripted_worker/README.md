# Tests of scripted_worker

The tests of the in-memory worker endpoint of `modules/extension/scripted_worker`.

## test_scripted_frames.cpp

| Test | What it proves |
| --- | --- |
| `Small_ScriptedFrames.YieldsItsFramesInOrderAndMarksTheLast` | A `ScriptedFrames` of two frames yields them in order, the second as the final frame, then reports it is finished. |
| `Small_ScriptedFrames.AWholeResultHoldsOneChunkPerOutputThenTheSuccess` | `whole_result()` of a dispatch with two outputs gives two buffer chunks and the task result. |

## test_scripted_worker.cpp

| Test | What it proves |
| --- | --- |
| `Medium_ScriptedWorker.ConnectingSendsAHelloNamingItsFacts` | A `ScriptedWorker` that reports the name `rack-7` sends the master a `HELLO` that carries it when it connects. |
