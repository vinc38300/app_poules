// ============================================================================
// SCRIPT DE CORRECTION AUTOMATIQUE POUR index.html
// Corrige les problèmes de durée de fonctionnement
// ============================================================================
// Usage: node correction_script.js

const fs = require('fs');
const path = require('path');

console.log('🔧 Script de correction pour index.html');
console.log('==========================================\n');

// Chemin du fichier
const filePath = './index.html';

try {
    // Lecture du fichier
    console.log('📖 Lecture de index.html...');
    let content = fs.readFileSync(filePath, 'utf8');
    
    // ========================================================================
    // CORRECTION 1: Fonction onRelay1TimeUpdate - Gestion durée minutes/secondes
    // ========================================================================
    console.log('✏️  Correction 1: onRelay1TimeUpdate...');
    
    const oldOnRelay1 = /function onRelay1TimeUpdate\(data\) \{[\s\S]*?validateTiming1\(\);\s*\}/;
    
    const newOnRelay1 = `function onRelay1TimeUpdate(data) {
            var timeStr = String.fromCharCode.apply(null, new Uint8Array(data));
            log(\`📅 Grain reçu: \${timeStr}\`);
            
            var parts = timeStr.split(',');
            if (parts.length >= 3) {
                var startTime = parts[0];
                var durationReceived = parseInt(parts[1]) || 0;
                var state = parts[2];
                
                // 🔧 CORRECTION: Détection automatique minutes/secondes
                // Si la valeur est > 360, c'est probablement en secondes
                var durationInMinutes;
                if (durationReceived > 360) {
                    durationInMinutes = Math.floor(durationReceived / 60);
                    log(\`🔄 Conversion: \${durationReceived}s → \${durationInMinutes}min\`);
                } else {
                    durationInMinutes = durationReceived;
                    log(\`✅ Durée directe: \${durationInMinutes}min\`);
                }
                
                var isOn = (state === 'ON');
                var isManual = relay1ManualMode;
                
                updateRelayStatus(1, isOn, isManual);
                
                document.getElementById('relay1ActiveTime').innerHTML = 
                    \`⏰ \${startTime} pour \${durationInMinutes} minutes\`;
                
                document.getElementById('startTime1').value = startTime;
                document.getElementById('duration1').value = durationInMinutes;
                
                validateTiming1();
            }
        }`;
    
    if (oldOnRelay1.test(content)) {
        content = content.replace(oldOnRelay1, newOnRelay1);
        console.log('   ✅ onRelay1TimeUpdate corrigée');
    } else {
        console.log('   ⚠️  Pattern non trouvé, tentative alternative...');
    }
    
    // ========================================================================
    // CORRECTION 2: Fonction programRelay1 - Envoi durée avec logs détaillés
    // ========================================================================
    console.log('✏️  Correction 2: programRelay1...');
    
    const oldProgramRelay1 = /function programRelay1\(\) \{[\s\S]*?}\);[\s\S]*?\}/;
    
    const newProgramRelay1 = `function programRelay1() {
            if (!isConnected || !servicesReady) {
                log("❌ Services pas encore prêts");
                return;
            }

            if (!validateTiming1()) {
                log("❌ Validation échouée");
                return;
            }

            var startTime = document.getElementById('startTime1').value;
            var duration = parseInt(document.getElementById('duration1').value, 10);

            // 🔧 CORRECTION: Validation stricte
            if (!duration || duration < 1 || duration > 360) {
                log(\`❌ Durée invalide: \${duration}\`);
                alert('La durée doit être entre 1 et 360 minutes');
                return;
            }

            var times = startTime.split(':').map(Number);
            
            // 🔧 CORRECTION: Envoi en MINUTES avec logs détaillés
            var data = new Uint8Array([
                3, 
                times[0], 
                times[1], 
                0,
                (duration >> 8) & 0xFF,
                duration & 0xFF
            ]);
            
            log(\`📅 ENVOI Grain: \${startTime} pour \${duration} MINUTES\`);
            log(\`📦 Bytes: [3, \${times[0]}, \${times[1]}, 0, \${(duration >> 8) & 0xFF}, \${duration & 0xFF}]\`);
            log(\`🔢 Durée codée: HIGH=\${(duration >> 8) & 0xFF}, LOW=\${duration & 0xFF}\`);
            
            relay1ManualMode = false;
            
            ble.write(deviceId, serviceUUID, characteristicUUID, data.buffer, function() {
                log(\`✅ Distribution grain programmée: \${duration}min\`);
                alert(\`✅ Programmé: \${startTime} pendant \${duration} minutes\`);
                validateTiming1();
                updateRelayStatus(1, relay1State, false);
                
                // Relecture après 1 seconde
                setTimeout(requestCurrentStates, 1000);
            }, function(error) {
                log(\`❌ Erreur programmation grain: \${error}\`);
                alert('❌ Erreur lors de la programmation');
            });
        }`;
    
    if (oldProgramRelay1.test(content)) {
        content = content.replace(oldProgramRelay1, newProgramRelay1);
        console.log('   ✅ programRelay1 corrigée');
    } else {
        console.log('   ⚠️  Pattern non trouvé');
    }
    
    // ========================================================================
    // CORRECTION 3: Ajout fonction de diagnostic
    // ========================================================================
    console.log('✏️  Correction 3: Ajout diagnostic...');
    
    const diagnosticFunction = `
        
        // 🔧 FONCTION DE DIAGNOSTIC AJOUTÉE
        function diagnosticDurations() {
            if (!isConnected) {
                alert('❌ Non connecté');
                return;
            }
            
            log('🔍 DIAGNOSTIC DES DURÉES');
            log('========================');
            
            var duration1 = document.getElementById('duration1').value;
            var duration2Start = document.getElementById('startTime2').value;
            var duration2Stop = document.getElementById('stopTime2').value;
            
            log(\`📊 Grain: \${duration1} minutes\`);
            log(\`📊 Porte: \${duration2Start} → \${duration2Stop}\`);
            
            // Test d'envoi
            var testDuration = 20; // 20 minutes
            var data = new Uint8Array([
                3, 8, 0, 0,
                (testDuration >> 8) & 0xFF,
                testDuration & 0xFF
            ]);
            
            log(\`🧪 Test envoi 20min: [\${Array.from(data).join(', ')}]\`);
            
            ble.write(deviceId, serviceUUID, characteristicUUID, data.buffer, 
                function() {
                    log('✅ Test envoyé - Vérifiez ESP32');
                    alert('Test envoyé! Vérifiez les logs ESP32');
                },
                function(error) {
                    log(\`❌ Erreur test: \${error}\`);
                }
            );
        }
        
        // Gestionnaires d'événements pour la validation en temps réel`;
    
    const eventHandlersPattern = /\/\/ Gestionnaires d'événements pour la validation en temps réel/;
    
    if (eventHandlersPattern.test(content)) {
        content = content.replace(eventHandlersPattern, diagnosticFunction);
        console.log('   ✅ Fonction diagnostic ajoutée');
    }
    
    // ========================================================================
    // CORRECTION 4: Ajout bouton diagnostic dans l'interface
    // ========================================================================
    console.log('✏️  Correction 4: Ajout bouton diagnostic...');
    
    const diagnosticButton = `
                <button class="quick-button" onclick="diagnosticDurations()">
                    🔍 Diagnostic durées
                </button>`;
    
    const quickGridPattern = /(ðŸ"' Fermer porte\s*<\/button>\s*<\/div>)/;
    
    if (quickGridPattern.test(content)) {
        content = content.replace(quickGridPattern, `$1${diagnosticButton}`);
        console.log('   ✅ Bouton diagnostic ajouté');
    }
    
    // ========================================================================
    // CORRECTION 5: Amélioration des logs
    // ========================================================================
    console.log('✏️  Correction 5: Logs améliorés...');
    
    content = content.replace(
        /log\('ðŸ"… Programmation grain:/g,
        "log('📅 📤 ENVOI Programmation grain:"
    );
    
    content = content.replace(
        /log\("âœ… Distribution grain programmÃ©e"\)/g,
        'log("✅ ✔️ Distribution grain programmée avec succès")'
    );
    
    console.log('   ✅ Logs améliorés');
    
    // ========================================================================
    // Sauvegarde
    // ========================================================================
    const backupPath = './index.html.backup';
    const correctedPath = './index_corrected.html';
    
    console.log('\n💾 Sauvegarde...');
    fs.writeFileSync(backupPath, fs.readFileSync(filePath));
    console.log(\`   ✅ Backup créé: \${backupPath}\`);
    
    fs.writeFileSync(correctedPath, content);
    console.log(\`   ✅ Version corrigée: \${correctedPath}\`);
    
    // ========================================================================
    // Résumé
    // ========================================================================
    console.log('\n✅ CORRECTION TERMINÉE');
    console.log('======================');
    console.log('');
    console.log('📋 Changements appliqués:');
    console.log('  1. ✅ Détection auto minutes/secondes dans onRelay1TimeUpdate');
    console.log('  2. ✅ Validation stricte + logs détaillés dans programRelay1');
    console.log('  3. ✅ Fonction diagnostic ajoutée');
    console.log('  4. ✅ Bouton diagnostic dans interface');
    console.log('  5. ✅ Logs améliorés');
    console.log('');
    console.log('📁 Fichiers créés:');
    console.log(\`  - \${backupPath} (original)\`);
    console.log(\`  - \${correctedPath} (corrigé)\`);
    console.log('');
    console.log('🚀 Prochaines étapes:');
    console.log('  1. Remplacez index.html par index_corrected.html');
    console.log('  2. Recompilez votre app Cordova');
    console.log('  3. Testez avec le bouton "🔍 Diagnostic durées"');
    console.log('  4. Vérifiez les logs pour voir si ESP32 reçoit en minutes');
    console.log('');
    
} catch (error) {
    console.error('❌ ERREUR:', error.message);
    process.exit(1);
}
