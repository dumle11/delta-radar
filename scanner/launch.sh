#!/system/bin/sh
# Legacy launcher intentionally fails closed. The app verifies the pinned build
# identity, owns its cancellation lease, and never restarts/debug-gates the game.
echo 'Use Delta Radar Start with scanner/deployment.properties; legacy launcher disabled.' >&2
exit 64
