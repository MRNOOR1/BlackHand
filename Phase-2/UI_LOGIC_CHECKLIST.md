# BlackHand UI Logic Checklist

## Global System

### Navigation
- [ ] UP moves selection up
- [ ] DOWN moves selection down
- [ ] CENTER selects item
- [ ] BACK returns to previous screen
- [ ] Cannot move selection beyond list bounds
- [ ] Selection remains visible when scrolling
- [ ] Selection resets correctly when entering screen

### Input Safety
- [ ] Buttons cannot trigger multiple actions when held
- [ ] Long press vs short press handled correctly
- [ ] Rapid button presses do not crash UI

### Screen Behavior
- [ ] Screen redraws correctly
- [ ] No flickering
- [ ] No UI elements overlap
- [ ] UI works on smallest screen size
- [ ] UI works on longest text names

### Storage
- [ ] Handles missing storage device
- [ ] Handles full storage
- [ ] Handles corrupted files
- [ ] Handles empty folders

### System State
- [ ] Music stops when entering call
- [ ] Recording stops when exiting app
- [ ] Playback stops when leaving player

## Notes App

### List Screen
- [ ] Notes list loads correctly
- [ ] Empty notes folder handled
- [ ] Long note titles display correctly
- [ ] Scrolling works with many notes

### Open Note
- [ ] Note content loads correctly
- [ ] Large note loads correctly
- [ ] Scrolling inside note works

### Create Note
- [ ] New note file created
- [ ] Cannot create note when storage full
- [ ] Empty note handled properly

### Edit Note
- [ ] Text updates correctly
- [ ] Cursor moves correctly
- [ ] Save works
- [ ] Cancel works

### Delete Note
- [ ] Delete confirmation shown
- [ ] Correct note deleted
- [ ] List refreshes after delete

### Edge Cases
- [ ] Editing extremely long line
- [ ] Editing empty note
- [ ] Opening corrupted note file

## Voice Memo

### Memo List
- [ ] Voice memo list loads
- [ ] Empty list handled
- [ ] Many memos scroll correctly

### Recording
- [ ] Recording starts
- [ ] Timer counts correctly
- [ ] Pause works
- [ ] Resume works
- [ ] Stop saves file

### Cancel Recording
- [ ] Cancel deletes temp file
- [ ] Cancel returns to list

### Playback
- [ ] Play starts audio
- [ ] Pause works
- [ ] Resume works
- [ ] Stop works

### File Handling
- [ ] Memo saved with unique name
- [ ] Memo duration displayed correctly

### Edge Cases
- [ ] Storage full during recording
- [ ] Recording interrupted
- [ ] Very long recording

## Music Player

### Library Scan
- [ ] Genres detected correctly
- [ ] Artists detected correctly
- [ ] Tracks detected correctly
- [ ] Missing folders handled

### Genre Menu
- [ ] Genre list loads
- [ ] Genre selection opens artists

### Artist Menu
- [ ] Artist list loads
- [ ] Artist selection opens tracks

### Track List
- [ ] Track list loads
- [ ] Selection works
- [ ] Large track list scrolls

### Playback
- [ ] Track starts correctly
- [ ] Pause works
- [ ] Resume works
- [ ] Stop works

### Player Controls
- [ ] Next track works
- [ ] Previous track works
- [ ] Volume changes work

### Track Info
- [ ] Title displayed
- [ ] Artist displayed
- [ ] Duration displayed
- [ ] Progress bar updates

### Edge Cases
- [ ] Corrupted mp3
- [ ] Unsupported format
- [ ] Empty genre folder
- [ ] Track deleted during playback

## Settings

### Settings Menu
- [ ] Settings list loads
- [ ] Navigation works

### Sound
- [ ] Volume adjustment works
- [ ] Volume persists after reboot

### Display
- [ ] Brightness adjustment works
- [ ] Screen timeout works

### Storage
- [ ] Storage usage displayed
- [ ] Handles storage errors

### System
- [ ] Device info displayed
- [ ] Restart works
- [ ] Factory reset confirmation works

## File System

### Notes
- [ ] Files created correctly
- [ ] Files deleted correctly
- [ ] Files updated correctly

### Voice Memos
- [ ] Files saved correctly
- [ ] Files playable

### Music
- [ ] Library scan correct
- [ ] Paths correct

## Performance
- [ ] UI responds instantly
- [ ] No input lag
- [ ] No memory leaks
- [ ] No crash after long usage
- [ ] Large libraries load correctly

## UI Edge Cases

### Text Handling
- [ ] Very long filenames
- [ ] Unicode characters
- [ ] Special characters

### Lists
- [ ] 0 items
- [ ] 1 item
- [ ] 100+ items

### Screen
- [ ] Text overflow handled
- [ ] UI remains aligned

## Final System Test

Boot -> Notes create/edit/delete -> Voice memo record/play/delete -> Music browse/play -> Settings change -> Reboot -> Data still exists
