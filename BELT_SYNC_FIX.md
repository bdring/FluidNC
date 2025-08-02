# Belt Length Synchronization Fix

## Problem Description

During long moves on Maslow CNC systems, belt tension was not maintained correctly. While belt tension was correct at the beginning and end of moves, belts would become loose during the move execution.

## Root Cause

The issue was caused by FluidNC's motion planning system linearly interpolating between start and end motor positions (belt lengths) during move execution. For Maslow CNC systems, this approach is problematic because:

1. The kinematic relationship between cartesian coordinates (X,Y,Z) and belt lengths is highly non-linear
2. Linear interpolation in motor space does not correspond to correct belt lengths for intermediate cartesian positions
3. During long moves, intermediate belt positions computed by linear interpolation do not maintain proper tension

## Solution Implemented

The fix implements automatic segmentation of long moves to ensure correct belt length calculation at intermediate points:

### Key Features:
- **Automatic Segmentation**: Moves longer than `maxSegmentLength` are automatically broken into smaller segments
- **Proper Kinematics**: Each segment has its belt lengths computed via correct kinematic transformation
- **Configurable**: The `maxSegmentLength` parameter allows tuning (default: 5mm)
- **Selective**: Only affects XY moves; Z-only moves and rapid motions are unaffected
- **Feed Rate Preservation**: Feed rate is properly distributed across segments to maintain timing

### Technical Implementation:
1. **Segmentation in Cartesian Space**: Uses `mc_linear()` to submit segments in cartesian coordinates
2. **Proper Kinematic Transformation**: Each segment passes through `cartesian_to_motors()` for correct belt length computation
3. **Recursion Prevention**: Prevents infinite loops during segmentation with internal flag
4. **Arc-Style Processing**: Follows the same pattern as FluidNC's existing arc segmentation

### Critical Fix Applied:
The initial implementation incorrectly used `mc_move_motors()` which operates in motor space, bypassing kinematic transformation. The fix changes to `mc_linear()` which operates in cartesian space and ensures each segment gets proper kinematic transformation for accurate belt lengths.

### Configuration:
```yaml
kinematics:
  MaslowKinematics:
    maxSegmentLength: 5.0  # Segment moves longer than 5mm
```

### Technical Implementation:
- Similar approach to arc segmentation already used in FluidNC
- Preserves existing behavior for calibration and Z-axis operations
- Upper limit (100 segments) prevents excessive segmentation on very long moves
- Uses existing motion planner infrastructure

## Usage Guidelines

- **Default Setting (5mm)**: Good balance for most applications
- **Smaller Values (2-3mm)**: Better belt tension maintenance, more computation
- **Larger Values (7-10mm)**: Less computation, may still have some belt slack on very long moves
- **Avoid < 1mm**: May cause excessive computation and choppy motion
- **Avoid > 20mm**: May not adequately solve belt slack issues

## Files Modified

1. `FluidNC/src/Kinematics/MaslowKinematics.h` - Added maxSegmentLength parameter
2. `FluidNC/src/Kinematics/MaslowKinematics.cpp` - Implemented segmentation logic
3. `FluidNC/test/Maslow/BeltLengthSynchronizationTest.cpp` - Added tests

## Testing

The fix includes comprehensive tests that validate:
- Non-linear kinematic behavior requiring segmentation
- Round-trip consistency of forward/inverse kinematics
- Reasonable belt length calculations
- Configuration parameter handling

## Impact

This fix resolves the belt slack issue during long moves while maintaining compatibility with existing FluidNC functionality. Users should see improved cut quality on long diagonal moves and better overall belt tension maintenance.